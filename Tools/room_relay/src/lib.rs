#![forbid(unsafe_code)]

use std::collections::HashMap;
use std::io;
use std::net::IpAddr;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};
use std::time::{Duration, Instant};

use futures_util::{SinkExt, StreamExt};
use rand::Rng;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt, ReadBuf};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{mpsc, Mutex};
use tokio_tungstenite::tungstenite::Message;

pub const PROTOCOL_VERSION: u16 = 5;
pub const MAX_PEERS_PER_ROOM: usize = 7;
pub const MAX_LAN_CANDIDATES: usize = 16;
pub const MAX_RELAY_PAYLOAD: usize = 2048;
pub const MAX_ROOMS_TOTAL: usize = 1024;
pub const MAX_ROOMS_PER_IP: usize = 32;
pub const DEFAULT_KEEPALIVE_INTERVAL: Duration = Duration::from_secs(15);
pub const DEFAULT_IDLE_TIMEOUT: Duration = Duration::from_secs(30 * 60);

pub const ROOM_ALPHABET: &[u8] = b"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

pub const OP_REGISTER_HOST: u8 = 0x01;
pub const OP_ROOM_ASSIGNED: u8 = 0x02;
pub const OP_JOIN_ROOM: u8 = 0x03;
pub const OP_JOIN_ACCEPTED: u8 = 0x04;
pub const OP_JOIN_REJECTED: u8 = 0x05;
pub const OP_RELAY_PACKET: u8 = 0x06;
pub const OP_PEER_DISCONNECTED: u8 = 0x07;
pub const OP_PING: u8 = 0x08;
pub const OP_PONG: u8 = 0x09;

pub const REJECT_ROOM_NOT_FOUND: u8 = 1;
pub const REJECT_ROOM_FULL: u8 = 2;
pub const REJECT_VERSION_MISMATCH: u8 = 3;

const OUTBOUND_QUEUE_CAP: usize = 512;

#[derive(Debug, Clone)]
enum OutboundMsg {
    Binary(Vec<u8>),
    Close,
}

type ConnTx = mpsc::Sender<OutboundMsg>;

#[derive(Debug)]
struct PeerEntry {
    tx: ConnTx,
    has_sent_packet: bool,
}

#[derive(Debug)]
struct Room {
    code: [u8; 4],
    host_ip: IpAddr,
    host_tx: ConnTx,
    lan_candidates: Vec<[u8; 6]>,
    peers: [Option<PeerEntry>; MAX_PEERS_PER_ROOM],
    last_activity: Instant,
}

#[derive(Debug, Default)]
struct RelayState {
    rooms: HashMap<[u8; 4], Room>,
}

#[derive(Debug, Clone)]
pub struct RelayConfig {
    pub keepalive_interval: Duration,
    pub idle_timeout: Duration,
}

impl Default for RelayConfig {
    fn default() -> Self {
        Self {
            keepalive_interval: DEFAULT_KEEPALIVE_INTERVAL,
            idle_timeout: DEFAULT_IDLE_TIMEOUT,
        }
    }
}

#[derive(Debug, Clone)]
enum ConnRole {
    Unregistered,
    Host { code: [u8; 4] },
    Client { code: [u8; 4], client_id: u8 },
}

struct PrefixedStream {
    prefix: Vec<u8>,
    offset: usize,
    inner: TcpStream,
}

impl PrefixedStream {
    fn new(prefix: Vec<u8>, inner: TcpStream) -> Self {
        Self {
            prefix,
            offset: 0,
            inner,
        }
    }
}

impl AsyncRead for PrefixedStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        if self.offset < self.prefix.len() {
            let remaining = &self.prefix[self.offset..];
            let to_copy = remaining.len().min(buf.remaining());
            buf.put_slice(&remaining[..to_copy]);
            self.offset += to_copy;
            return Poll::Ready(Ok(()));
        }
        Pin::new(&mut self.inner).poll_read(cx, buf)
    }
}

impl AsyncWrite for PrefixedStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        data: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.inner).poll_write(cx, data)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_shutdown(cx)
    }
}

fn send_binary_best_effort(tx: &ConnTx, payload: Vec<u8>) {
    let _ = tx.try_send(OutboundMsg::Binary(payload));
}

fn send_close_best_effort(tx: &ConnTx) {
    let _ = tx.try_send(OutboundMsg::Close);
}

fn normalize_room_code(raw: &[u8]) -> Option<[u8; 4]> {
    if raw.len() != 4 {
        return None;
    }
    let mut out = [0u8; 4];
    for (i, &b) in raw.iter().enumerate() {
        let up = b.to_ascii_uppercase();
        if !up.is_ascii_alphanumeric() {
            return None;
        }
        out[i] = up;
    }
    Some(out)
}

fn generate_unique_code(rooms: &HashMap<[u8; 4], Room>) -> Option<[u8; 4]> {
    let mut rng = rand::thread_rng();
    for _ in 0..512 {
        let mut code = [0u8; 4];
        for slot in &mut code {
            let idx = rng.gen_range(0..ROOM_ALPHABET.len());
            *slot = ROOM_ALPHABET[idx];
        }
        if !rooms.contains_key(&code) {
            return Some(code);
        }
    }
    None
}

async fn cleanup_idle_rooms(state: &Arc<Mutex<RelayState>>, idle_timeout: Duration) {
    let now = Instant::now();
    let mut guard = state.lock().await;
    let expired: Vec<[u8; 4]> = guard
        .rooms
        .iter()
        .filter_map(|(code, room)| {
            if now.duration_since(room.last_activity) > idle_timeout {
                Some(*code)
            } else {
                None
            }
        })
        .collect();

    for code in expired {
        if let Some(room) = guard.rooms.remove(&code) {
            send_close_best_effort(&room.host_tx);
            for peer_opt in room.peers.into_iter() {
                if let Some(peer) = peer_opt {
                    send_close_best_effort(&peer.tx);
                }
            }
        }
    }
}

pub async fn serve_listener(
    listener: TcpListener,
    config: RelayConfig,
    mut shutdown_rx: tokio::sync::watch::Receiver<bool>,
) -> io::Result<()> {
    let state = Arc::new(Mutex::new(RelayState::default()));

    let cleanup_state = Arc::clone(&state);
    let idle_timeout = config.idle_timeout;
    let sweep_period = (idle_timeout / 4).max(Duration::from_millis(100)).min(Duration::from_secs(60));
    let mut cleanup_shutdown = shutdown_rx.clone();
    let cleanup_handle = tokio::spawn(async move {
        let mut ticker = tokio::time::interval(sweep_period);
        ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tokio::select! {
                _ = ticker.tick() => {
                    cleanup_idle_rooms(&cleanup_state, idle_timeout).await;
                }
                _ = cleanup_shutdown.changed() => {
                    if *cleanup_shutdown.borrow() {
                        break;
                    }
                }
            }
        }
    });

    loop {
        tokio::select! {
            accept_res = listener.accept() => {
                let (stream, remote_addr) = accept_res?;
                let _ = stream.set_nodelay(true);
                let conn_state = Arc::clone(&state);
                let conn_cfg = config.clone();
                tokio::spawn(async move {
                    handle_tcp_connection(stream, remote_addr.ip(), conn_state, conn_cfg).await;
                });
            }
            _ = shutdown_rx.changed() => {
                if *shutdown_rx.borrow() {
                    break;
                }
            }
        }
    }

    cleanup_handle.abort();
    Ok(())
}

fn find_header_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4)
        .position(|w| w == b"\r\n\r\n")
        .map(|pos| pos + 4)
}

async fn handle_tcp_connection(
    mut stream: TcpStream,
    remote_ip: IpAddr,
    state: Arc<Mutex<RelayState>>,
    config: RelayConfig,
) {
    let mut req_buf = Vec::with_capacity(1024);
    let mut tmp = [0u8; 1024];

    let read_res = tokio::time::timeout(Duration::from_secs(5), async {
        while find_header_end(&req_buf).is_none() {
            if req_buf.len() >= 4096 {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "header too large"));
            }
            let n = stream.read(&mut tmp).await?;
            if n == 0 {
                return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "eof before headers"));
            }
            req_buf.extend_from_slice(&tmp[..n]);
        }
        Ok(())
    })
    .await;

    match read_res {
        Ok(Ok(())) => {}
        _ => return,
    }

    let hdr_end = match find_header_end(&req_buf) {
        Some(idx) => idx,
        None => return,
    };

    let hdr_str = String::from_utf8_lossy(&req_buf[..hdr_end]);
    let mut lines = hdr_str.split("\r\n");
    let request_line = lines.next().unwrap_or("");
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or("");
    let raw_target = parts.next().unwrap_or("");
    let path = raw_target.split('?').next().unwrap_or(raw_target);

    let mut is_ws_upgrade = false;
    for line in lines {
        if let Some((name, val)) = line.split_once(':') {
            if name.trim().eq_ignore_ascii_case("upgrade")
                && val.trim().eq_ignore_ascii_case("websocket")
            {
                is_ws_upgrade = true;
            }
        }
    }

    if is_ws_upgrade {
        if path == "/ws" || path.starts_with("/ws/") {
            let prefixed = PrefixedStream::new(req_buf, stream);
            if let Ok(ws_stream) = tokio_tungstenite::accept_async(prefixed).await {
                run_ws_session(ws_stream, remote_ip, state, config).await;
            }
        } else {
            let _ = stream
                .write_all(
                    b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found",
                )
                .await;
            let _ = stream.flush().await;
        }
        return;
    }

    if (method == "GET" || method == "HEAD") && (path == "/" || path == "/health") {
        let resp: &[u8] = if method == "HEAD" {
            b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n"
        } else {
            b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\nok\n"
        };
        let _ = stream.write_all(resp).await;
        let _ = stream.flush().await;
    } else {
        let _ = stream
            .write_all(
                b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found",
            )
            .await;
        let _ = stream.flush().await;
    }
}

async fn run_ws_session<S>(
    ws_stream: tokio_tungstenite::WebSocketStream<S>,
    remote_ip: IpAddr,
    state: Arc<Mutex<RelayState>>,
    config: RelayConfig,
) where
    S: AsyncRead + AsyncWrite + Unpin + Send + 'static,
{
    let (mut ws_write, mut ws_read) = ws_stream.split();
    let (tx, mut rx) = mpsc::channel::<OutboundMsg>(OUTBOUND_QUEUE_CAP);

    let keepalive_interval = config.keepalive_interval;
    let writer_handle = tokio::spawn(async move {
        let mut ping_ticker = tokio::time::interval(keepalive_interval);
        ping_ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        // Consume initial immediate tick
        ping_ticker.tick().await;

        loop {
            tokio::select! {
                maybe_msg = rx.recv() => {
                    match maybe_msg {
                        Some(OutboundMsg::Binary(bytes)) => {
                            if ws_write.send(Message::Binary(bytes)).await.is_err() {
                                break;
                            }
                        }
                        Some(OutboundMsg::Close) | None => {
                            let _ = ws_write.send(Message::Close(None)).await;
                            let _ = ws_write.close().await;
                            break;
                        }
                    }
                }
                _ = ping_ticker.tick() => {
                    if ws_write.send(Message::Ping(Vec::new())).await.is_err() {
                        break;
                    }
                    if ws_write.send(Message::Binary(vec![OP_PING])).await.is_err() {
                        break;
                    }
                }
            }
        }
    });

    let mut role = ConnRole::Unregistered;

    while let Some(frame_res) = ws_read.next().await {
        let msg = match frame_res {
            Ok(m) => m,
            Err(_) => break,
        };

        match msg {
            Message::Binary(data) => {
                if data.is_empty() {
                    continue;
                }
                let op = data[0];
                if op == OP_PING {
                    send_binary_best_effort(&tx, vec![OP_PONG]);
                    touch_room_activity(&state, &role).await;
                    continue;
                }
                if op == OP_PONG {
                    touch_room_activity(&state, &role).await;
                    continue;
                }

                let should_close =
                    handle_binary_frame(op, &data, remote_ip, &tx, &mut role, &state).await;
                if should_close {
                    break;
                }
            }
            Message::Ping(_) => {
                // tungstenite automatically replies with Pong on flush/read, and we touch activity
                touch_room_activity(&state, &role).await;
            }
            Message::Pong(_) => {
                touch_room_activity(&state, &role).await;
            }
            Message::Close(_) => {
                break;
            }
            Message::Text(_) | Message::Frame(_) => {}
        }
    }

    cleanup_connection(&role, &state).await;
    drop(tx);
    let _ = writer_handle.await;
}

async fn touch_room_activity(state: &Arc<Mutex<RelayState>>, role: &ConnRole) {
    let code = match role {
        ConnRole::Host { code } | ConnRole::Client { code, .. } => *code,
        ConnRole::Unregistered => return,
    };
    let mut guard = state.lock().await;
    if let Some(room) = guard.rooms.get_mut(&code) {
        room.last_activity = Instant::now();
    }
}

async fn handle_binary_frame(
    op: u8,
    data: &[u8],
    remote_ip: IpAddr,
    tx: &ConnTx,
    role: &mut ConnRole,
    state: &Arc<Mutex<RelayState>>,
) -> bool {
    match op {
        OP_REGISTER_HOST => {
            if !matches!(role, ConnRole::Unregistered) {
                return false;
            }
            if data.len() < 4 {
                return true;
            }
            let version = u16::from_le_bytes([data[1], data[2]]);
            if version != PROTOCOL_VERSION {
                send_binary_best_effort(tx, vec![OP_JOIN_REJECTED, REJECT_VERSION_MISMATCH]);
                return true;
            }
            let cand_count = data[3] as usize;
            if cand_count > MAX_LAN_CANDIDATES || data.len() < 4 + cand_count * 6 {
                return true;
            }
            let mut candidates = Vec::with_capacity(cand_count);
            for i in 0..cand_count {
                let off = 4 + i * 6;
                let mut entry = [0u8; 6];
                entry.copy_from_slice(&data[off..off + 6]);
                candidates.push(entry);
            }

            let mut guard = state.lock().await;
            if guard.rooms.len() >= MAX_ROOMS_TOTAL {
                return true;
            }
            let ip_rooms = guard
                .rooms
                .values()
                .filter(|r| r.host_ip == remote_ip)
                .count();
            if ip_rooms >= MAX_ROOMS_PER_IP {
                return true;
            }

            let code = match generate_unique_code(&guard.rooms) {
                Some(c) => c,
                None => return true,
            };

            guard.rooms.insert(
                code,
                Room {
                    code,
                    host_ip: remote_ip,
                    host_tx: tx.clone(),
                    lan_candidates: candidates,
                    peers: std::array::from_fn(|_| None),
                    last_activity: Instant::now(),
                },
            );
            drop(guard);

            *role = ConnRole::Host { code };
            let mut resp = Vec::with_capacity(5);
            resp.push(OP_ROOM_ASSIGNED);
            resp.extend_from_slice(&code);
            send_binary_best_effort(tx, resp);
            false
        }

        OP_JOIN_ROOM => {
            if !matches!(role, ConnRole::Unregistered) {
                return false;
            }
            if data.len() < 7 {
                return true;
            }
            let version = u16::from_le_bytes([data[1], data[2]]);
            if version != PROTOCOL_VERSION {
                send_binary_best_effort(tx, vec![OP_JOIN_REJECTED, REJECT_VERSION_MISMATCH]);
                return false;
            }
            let code = match normalize_room_code(&data[3..7]) {
                Some(c) => c,
                None => {
                    send_binary_best_effort(tx, vec![OP_JOIN_REJECTED, REJECT_ROOM_NOT_FOUND]);
                    return false;
                }
            };

            let mut guard = state.lock().await;
            let room = match guard.rooms.get_mut(&code) {
                Some(r) => r,
                None => {
                    send_binary_best_effort(tx, vec![OP_JOIN_REJECTED, REJECT_ROOM_NOT_FOUND]);
                    return false;
                }
            };

            let mut free_idx = None;
            for (idx, slot) in room.peers.iter().enumerate() {
                if slot.is_none() {
                    free_idx = Some(idx);
                    break;
                }
            }

            let slot_idx = match free_idx {
                Some(idx) => idx,
                None => {
                    send_binary_best_effort(tx, vec![OP_JOIN_REJECTED, REJECT_ROOM_FULL]);
                    return false;
                }
            };

            let client_id = (slot_idx + 1) as u8;
            room.peers[slot_idx] = Some(PeerEntry {
                tx: tx.clone(),
                has_sent_packet: false,
            });
            room.last_activity = Instant::now();

            let cand_count = room.lan_candidates.len().min(MAX_LAN_CANDIDATES);
            let mut resp = Vec::with_capacity(7 + cand_count * 6);
            resp.push(OP_JOIN_ACCEPTED);
            resp.extend_from_slice(&room.code);
            resp.push(client_id);
            resp.push(cand_count as u8);
            for cand in room.lan_candidates.iter().take(cand_count) {
                resp.extend_from_slice(cand);
            }
            drop(guard);

            *role = ConnRole::Client { code, client_id };
            send_binary_best_effort(tx, resp);
            false
        }

        OP_RELAY_PACKET => {
            if data.len() < 3 || data.len() - 2 > MAX_RELAY_PAYLOAD {
                return false;
            }
            let payload = &data[2..];

            match *role {
                ConnRole::Host { code } => {
                    let target_id = data[1];
                    if target_id == 0 || (target_id as usize) > MAX_PEERS_PER_ROOM {
                        return false;
                    }
                    let mut guard = state.lock().await;
                    if let Some(room) = guard.rooms.get_mut(&code) {
                        room.last_activity = Instant::now();
                        if let Some(peer) = &room.peers[(target_id - 1) as usize] {
                            let mut fwd = Vec::with_capacity(2 + payload.len());
                            fwd.push(OP_RELAY_PACKET);
                            fwd.push(0);
                            fwd.extend_from_slice(payload);
                            send_binary_best_effort(&peer.tx, fwd);
                        }
                    }
                }
                ConnRole::Client { code, client_id } => {
                    let mut guard = state.lock().await;
                    if let Some(room) = guard.rooms.get_mut(&code) {
                        room.last_activity = Instant::now();
                        if let Some(peer) = room.peers[(client_id - 1) as usize].as_mut() {
                            peer.has_sent_packet = true;
                        }
                        let mut fwd = Vec::with_capacity(2 + payload.len());
                        fwd.push(OP_RELAY_PACKET);
                        fwd.push(client_id);
                        fwd.extend_from_slice(payload);
                        send_binary_best_effort(&room.host_tx, fwd);
                    }
                }
                ConnRole::Unregistered => {}
            }
            false
        }

        OP_PEER_DISCONNECTED => {
            if data.len() < 2 {
                return false;
            }
            if let ConnRole::Host { code } = *role {
                let target_id = data[1];
                if target_id >= 1 && (target_id as usize) <= MAX_PEERS_PER_ROOM {
                    let mut guard = state.lock().await;
                    if let Some(room) = guard.rooms.get_mut(&code) {
                        room.last_activity = Instant::now();
                        if let Some(peer) = room.peers[(target_id - 1) as usize].take() {
                            send_close_best_effort(&peer.tx);
                        }
                    }
                }
            }
            false
        }

        _ => false,
    }
}

async fn cleanup_connection(role: &ConnRole, state: &Arc<Mutex<RelayState>>) {
    match *role {
        ConnRole::Unregistered => {}
        ConnRole::Host { code } => {
            let mut guard = state.lock().await;
            if let Some(room) = guard.rooms.remove(&code) {
                for peer_opt in room.peers.into_iter() {
                    if let Some(peer) = peer_opt {
                        send_close_best_effort(&peer.tx);
                    }
                }
            }
        }
        ConnRole::Client { code, client_id } => {
            let mut guard = state.lock().await;
            if let Some(room) = guard.rooms.get_mut(&code) {
                let idx = (client_id - 1) as usize;
                if idx < MAX_PEERS_PER_ROOM {
                    if let Some(peer) = room.peers[idx].take() {
                        if peer.has_sent_packet {
                            send_binary_best_effort(
                                &room.host_tx,
                                vec![OP_PEER_DISCONNECTED, client_id],
                            );
                        }
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::sync::watch;
    use tokio_tungstenite::{connect_async, MaybeTlsStream, WebSocketStream};

    type WsClient = WebSocketStream<MaybeTlsStream<TcpStream>>;

    async fn spawn_test_relay(
        config: RelayConfig,
    ) -> (std::net::SocketAddr, watch::Sender<bool>, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let (tx, rx) = watch::channel(false);
        let handle = tokio::spawn(async move {
            let _ = serve_listener(listener, config, rx).await;
        });
        (addr, tx, handle)
    }

    async fn connect_ws(addr: std::net::SocketAddr) -> WsClient {
        let url = format!("ws://{}/ws", addr);
        let (ws, _) = connect_async(&url).await.unwrap();
        ws
    }

    async fn recv_bin(ws: &mut WsClient) -> Vec<u8> {
        loop {
            match tokio::time::timeout(Duration::from_secs(2), ws.next())
                .await
                .expect("timed out waiting for ws frame")
            {
                Some(Ok(Message::Binary(b))) => {
                    if b.first() == Some(&OP_PING) {
                        continue;
                    }
                    return b;
                }
                Some(Ok(Message::Ping(_))) | Some(Ok(Message::Pong(_))) => continue,
                other => panic!("unexpected ws message: {:?}", other),
            }
        }
    }

    fn build_register_host(version: u16, candidates: &[([u8; 4], u16)]) -> Vec<u8> {
        let mut out = Vec::new();
        out.push(OP_REGISTER_HOST);
        out.extend_from_slice(&version.to_le_bytes());
        out.push(candidates.len() as u8);
        for (ip, port) in candidates {
            out.extend_from_slice(ip);
            out.extend_from_slice(&port.to_le_bytes());
        }
        out
    }

    fn build_join_room(version: u16, code: &[u8; 4]) -> Vec<u8> {
        let mut out = Vec::new();
        out.push(OP_JOIN_ROOM);
        out.extend_from_slice(&version.to_le_bytes());
        out.extend_from_slice(code);
        out
    }

    #[tokio::test]
    async fn healthcheck_and_404() {
        let (addr, stop_tx, handle) = spawn_test_relay(RelayConfig::default()).await;

        let mut s = TcpStream::connect(addr).await.unwrap();
        s.write_all(b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n")
            .await
            .unwrap();
        let mut resp = String::new();
        s.read_to_string(&mut resp).await.unwrap();
        assert!(resp.starts_with("HTTP/1.1 200 OK"), "got: {resp}");
        assert!(resp.ends_with("ok\n"), "got: {resp}");

        let mut s2 = TcpStream::connect(addr).await.unwrap();
        s2.write_all(b"GET /unknown HTTP/1.1\r\nHost: localhost\r\n\r\n")
            .await
            .unwrap();
        let mut resp2 = String::new();
        s2.read_to_string(&mut resp2).await.unwrap();
        assert!(resp2.starts_with("HTTP/1.1 404 Not Found"), "got: {resp2}");

        let _ = stop_tx.send(true);
        let _ = handle.await;
    }

    #[tokio::test]
    async fn room_allocation_routing_and_disconnects() {
        let (addr, stop_tx, handle) = spawn_test_relay(RelayConfig::default()).await;

        let mut host = connect_ws(addr).await;
        let cands = [([192, 168, 0, 50], 29400), ([127, 0, 0, 1], 29400)];
        host.send(Message::Binary(build_register_host(PROTOCOL_VERSION, &cands)))
            .await
            .unwrap();

        let assigned = recv_bin(&mut host).await;
        assert_eq!(assigned.len(), 5);
        assert_eq!(assigned[0], OP_ROOM_ASSIGNED);
        let code: [u8; 4] = assigned[1..5].try_into().unwrap();
        assert!(code.iter().all(|b| ROOM_ALPHABET.contains(b)));

        // Ping/Pong check
        host.send(Message::Binary(vec![OP_PING])).await.unwrap();
        assert_eq!(recv_bin(&mut host).await, vec![OP_PONG]);

        // Join with lowercase code + valid version
        let lower_code: [u8; 4] = [
            code[0].to_ascii_lowercase(),
            code[1].to_ascii_lowercase(),
            code[2].to_ascii_lowercase(),
            code[3].to_ascii_lowercase(),
        ];
        let mut client1 = connect_ws(addr).await;
        client1
            .send(Message::Binary(build_join_room(PROTOCOL_VERSION, &lower_code)))
            .await
            .unwrap();
        let join1 = recv_bin(&mut client1).await;
        assert_eq!(join1[0], OP_JOIN_ACCEPTED);
        assert_eq!(&join1[1..5], &code);
        assert_eq!(join1[5], 1);
        assert_eq!(join1[6], 2);
        assert_eq!(&join1[7..11], &[192, 168, 0, 50]);
        assert_eq!(u16::from_le_bytes([join1[11], join1[12]]), 29400);

        // Silent probe-only client joins and disconnects without sending packets -> host NOT notified
        let mut probe_client = connect_ws(addr).await;
        probe_client
            .send(Message::Binary(build_join_room(PROTOCOL_VERSION, &code)))
            .await
            .unwrap();
        let join_probe = recv_bin(&mut probe_client).await;
        assert_eq!(join_probe[5], 2);
        let _ = probe_client.close(None).await;
        drop(probe_client);
        tokio::time::sleep(Duration::from_millis(50)).await;

        // Client 1 sends packet -> Host receives with client_id=1
        client1
            .send(Message::Binary(vec![OP_RELAY_PACKET, 99, 10, 20, 30]))
            .await
            .unwrap();
        let to_host = recv_bin(&mut host).await;
        assert_eq!(to_host, vec![OP_RELAY_PACKET, 1, 10, 20, 30]);

        // Host sends packet to client 1 -> Client 1 receives with sender=0
        host.send(Message::Binary(vec![OP_RELAY_PACKET, 1, 44, 55]))
            .await
            .unwrap();
        let to_c1 = recv_bin(&mut client1).await;
        assert_eq!(to_c1, vec![OP_RELAY_PACKET, 0, 44, 55]);

        // Active Client 1 closes -> Host receives PeerDisconnected(1)
        let _ = client1.close(None).await;
        drop(client1);
        let disc = recv_bin(&mut host).await;
        assert_eq!(disc, vec![OP_PEER_DISCONNECTED, 1]);

        let _ = stop_tx.send(true);
        let _ = handle.await;
    }

    #[tokio::test]
    async fn rejections_and_host_disconnect() {
        let (addr, stop_tx, handle) = spawn_test_relay(RelayConfig::default()).await;

        // Room not found
        let mut c = connect_ws(addr).await;
        c.send(Message::Binary(build_join_room(PROTOCOL_VERSION, b"ZZZZ")))
            .await
            .unwrap();
        assert_eq!(recv_bin(&mut c).await, vec![OP_JOIN_REJECTED, REJECT_ROOM_NOT_FOUND]);

        // Version mismatch
        c.send(Message::Binary(build_join_room(PROTOCOL_VERSION + 1, b"ZZZZ")))
            .await
            .unwrap();
        assert_eq!(recv_bin(&mut c).await, vec![OP_JOIN_REJECTED, REJECT_VERSION_MISMATCH]);

        // Host registers room, fills all 7 slots, 8th is rejected
        let mut host = connect_ws(addr).await;
        host.send(Message::Binary(build_register_host(PROTOCOL_VERSION, &[])))
            .await
            .unwrap();
        let assigned = recv_bin(&mut host).await;
        let code: [u8; 4] = assigned[1..5].try_into().unwrap();

        let mut clients = Vec::new();
        for expected_id in 1..=MAX_PEERS_PER_ROOM as u8 {
            let mut cl = connect_ws(addr).await;
            cl.send(Message::Binary(build_join_room(PROTOCOL_VERSION, &code)))
                .await
                .unwrap();
            let ack = recv_bin(&mut cl).await;
            assert_eq!(ack[0], OP_JOIN_ACCEPTED);
            assert_eq!(ack[5], expected_id);
            clients.push(cl);
        }

        let mut extra = connect_ws(addr).await;
        extra
            .send(Message::Binary(build_join_room(PROTOCOL_VERSION, &code)))
            .await
            .unwrap();
        assert_eq!(recv_bin(&mut extra).await, vec![OP_JOIN_REJECTED, REJECT_ROOM_FULL]);

        // Host disconnects -> peer sees Close
        let _ = host.close(None).await;
        drop(host);
        let next_msg = tokio::time::timeout(Duration::from_secs(2), clients[0].next())
            .await
            .expect("peer should close when host leaves");
        assert!(matches!(next_msg, Some(Ok(Message::Close(_))) | None));

        let _ = stop_tx.send(true);
        let _ = handle.await;
    }

    #[tokio::test]
    async fn idle_room_cleanup() {
        let cfg = RelayConfig {
            keepalive_interval: Duration::from_secs(30),
            idle_timeout: Duration::from_millis(120),
        };
        let (addr, stop_tx, handle) = spawn_test_relay(cfg).await;

        let mut host = connect_ws(addr).await;
        host.send(Message::Binary(build_register_host(PROTOCOL_VERSION, &[])))
            .await
            .unwrap();
        let assigned = recv_bin(&mut host).await;
        let code: [u8; 4] = assigned[1..5].try_into().unwrap();

        tokio::time::sleep(Duration::from_millis(350)).await;

        let mut late = connect_ws(addr).await;
        late.send(Message::Binary(build_join_room(PROTOCOL_VERSION, &code)))
            .await
            .unwrap();
        assert_eq!(
            recv_bin(&mut late).await,
            vec![OP_JOIN_REJECTED, REJECT_ROOM_NOT_FOUND]
        );

        let _ = stop_tx.send(true);
        let _ = handle.await;
    }
}
