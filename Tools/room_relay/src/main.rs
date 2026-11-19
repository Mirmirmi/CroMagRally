#![forbid(unsafe_code)]

use std::env;
use std::process;

use room_relay::{serve_listener, RelayConfig};
use tokio::net::TcpListener;
use tokio::sync::watch;

fn parse_bind_addr() -> String {
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        if arg == "--bind" {
            if let Some(val) = args.next() {
                return val;
            }
            eprintln!("--bind requires an address argument");
            process::exit(2);
        } else if let Some(val) = arg.strip_prefix("--bind=") {
            return val.to_string();
        }
    }
    env::var("CROMAG_RELAY_BIND").unwrap_or_else(|_| "127.0.0.1:29402".to_string())
}

#[tokio::main]
async fn main() {
    let bind_addr = parse_bind_addr();
    let listener = match TcpListener::bind(&bind_addr).await {
        Ok(l) => l,
        Err(err) => {
            eprintln!("room_relay: failed to bind {}: {}", bind_addr, err);
            process::exit(1);
        }
    };
    let actual_addr = listener
        .local_addr()
        .map(|a| a.to_string())
        .unwrap_or_else(|_| bind_addr.clone());
    println!("room_relay listening on {}", actual_addr);

    let (shutdown_tx, shutdown_rx) = watch::channel(false);
    tokio::spawn(async move {
        let _ = tokio::signal::ctrl_c().await;
        let _ = shutdown_tx.send(true);
    });

    if let Err(err) = serve_listener(listener, RelayConfig::default(), shutdown_rx).await {
        eprintln!("room_relay error: {}", err);
        process::exit(1);
    }
}
