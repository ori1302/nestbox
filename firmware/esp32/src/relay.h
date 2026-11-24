// Webhooks through the NestBox relay: a task that keeps the device's WebSocket to its mailbox open while it has the
// keys (engine/host/relay.h), and tells the pet each webhook that carries the right secret.
#pragma once

class App;

// Starts the relay task once the device has relay keys (called often; it starts once). TLS buffers go to PSRAM from
// then on.
void follow_relay(App* app);
