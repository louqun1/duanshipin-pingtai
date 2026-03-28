# AGENTS.md

## Project positioning
This project is a unified audio/video platform prototype.

It has three core modules:
1. self-built player kernel (Qt + FFmpeg + C++)
2. VOD pipeline (upload / transcode / HLS / playback)
3. live streaming + multi-user mic connection (RTMP ingest + HTTP-FLV watch + WebRTC interaction)

## My learning priority
My primary learning goal is audio/video engineering, not generic product features.

## Learning-core tasks
The following are learning-core tasks:
- player kernel internals
- demux / decode / A/V sync / seek
- VOD media pipeline
- RTMP ingest
- HTTP-FLV playback
- WebRTC signaling / room flow / mic-up flow

For learning-core tasks:
- do not directly implement the whole feature
- explain the data flow first
- break work into small milestones
- give skeleton + TODO instead of full implementation
- clearly mark what I should handwrite
- after I write code, only review; do not rewrite unless I ask

## Delivery-only tasks
The following can be generated more directly:
- CRUD
- DTO / boilerplate
- route registration
- config loading
- logging / middleware
- simple admin pages

## Task style
For complex tasks, plan first before coding.
Keep changes small and reviewable.
Always include how to verify the change.