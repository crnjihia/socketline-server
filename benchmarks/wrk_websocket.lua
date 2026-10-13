-- wrk_websocket.lua: HTTP upgrade to WebSocket benchmark script
wrk.method = "GET"
wrk.path = "/"
wrk.headers["Upgrade"] = "websocket"
wrk.headers["Connection"] = "Upgrade"
wrk.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ=="
wrk.headers["Sec-WebSocket-Version"] = "13"
