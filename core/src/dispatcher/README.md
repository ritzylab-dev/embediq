# dispatcher

Reserved for Phase 2 sub-function dispatcher implementation.

The sub-function dispatcher will allow multiple handlers to register
for the same message type within a single FB, with configurable
fan-out and consume/stop-propagation semantics.

Phase 1 sub-fn dispatch is handled inline in fb_engine.c.
This directory is intentionally empty until Phase 2 implementation begins.
