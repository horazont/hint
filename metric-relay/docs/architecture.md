# Metric Relay Architecture

## High-level architecture

```
 Source 1  ---
              \                /---- Sink 1
 Source 2  ----==== Broker ====
              /                \---- Sink 2
 Source 3  ---
```

## Broker pipeline

### Sample batches

1. Rewrite
2. Copy to each sink

### Streams

1. Copy to each sink

## Persistent queues

Persistent queues ensure that a sample or stream block has been written to disk before it is acknowledged to the sender. To implement this, sources have to block on the input until the broker has acknowledged comitting the data. This may not be possible/advisable with SNURL endpoints.
