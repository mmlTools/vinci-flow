# VinciFlow -- Lower Thirds Orchestration for OBS Studio

![Version](https://img.shields.io/github/v/release/mmltools/vinci-flow)
![OBS](https://img.shields.io/badge/OBS-Studio%2029%2B-informational)
![Platform](https://img.shields.io/badge/Platform-Windows%2064--bit-blue)
![License](https://img.shields.io/badge/License-GPLv2-green)
![WebSocket](https://img.shields.io/badge/obs--websocket-Vendor%20API-orange)
![HTML](https://img.shields.io/badge/Overlay-HTML%2FCSS%2FJS-purple)
![Status](https://img.shields.io/badge/State-Production%20Ready-brightgreen)

**VinciFlow** is a professional lower-third orchestration engine for OBS
Studio, built to deliver broadcast-grade control, timing precision, and
visual consistency in live productions.

It unifies a real-time dock UI, an HTML overlay renderer, hotkey
automation, obs-websocket vendor commands, and a JSON-driven runtime
data API into a single, coherent system for managing on-screen identity,
motion, and live information.

📘 **Full Documentation:**\
https://vinciflow.streamrsc.com/

------------------------------------------------------------------------

## Concept

VinciFlow treats lower thirds as timed visual elements in a controlled
flow:

-   Each title is a reusable visual unit.
-   Each group is a timed or randomized sequence.
-   The plugin conducts entrance, exit, transitions, and visibility.
-   The overlay engine renders them deterministically via HTML
    templates.
-   External applications can inject live data via JSON without
    interrupting animations.

No local servers. No scene duplication.\
All output is generated as a local HTML file and consumed by an OBS
Browser Source.

------------------------------------------------------------------------

## Capabilities

### Lower Third Engine

-   Unlimited structured items (title, subtitle, avatar, colors, fonts,
    radius, opacity).
-   Independent animation in/out with optional sound cues.
-   Template-driven rendering using HTML, CSS, and JavaScript.
-   Custom or Animate.css-based animation pipelines.

### Runtime Parameters (API Template)

-   Per-lower-third JSON interface for live data (scores, names,
    counters, stats, etc.).

-   Asynchronous polling with change-only DOM updates (no animation
    disruption).

-   Automatic generation of:

    -   `parameters_lt_<LOWER_THIRD_ID>.json`
    -   Optional shared `parameters.json` (ID-mapped).

-   Safe atomic file writes and per-LT isolation.

-   Data binding via simple HTML attributes:

    ``` html
    <span data-score></span>
    <span data-player></span>
    ```

### Group Sequencing

-   Linear or randomized playback.
-   Precise timing control (duration, delay, looping, exclusivity).
-   Ideal for sponsors, speakers, social rotators, team lineups, and
    show segments.

### Overlay Output

-   Single active HTML overlay with automatic refresh.
-   Browser Source binding and lifecycle management.
-   No HTTP, no ports, no network latency.

### Control Layer

-   Native OBS Dock UI.
-   Global and per-item hotkeys.
-   obs-websocket Vendor API for automation, bots, and control surfaces.

### Theming & Extensibility

-   Fully custom themes via HTML/CSS/JS.
-   Placeholder substitution at render time.
-   Custom animation hooks and sound triggers.
-   Runtime data binding via API Templates.

------------------------------------------------------------------------

## Architecture

-   **UI:** Qt Dock (OBS Frontend API)\
-   **Core:** C++ Orchestration Engine\
-   **Renderer:** HTML Template Compiler\
-   **Automation:** obs-websocket Vendor Interface\
-   **Runtime Data:** JSON Polling Engine (Async, Change-Only)\
-   **Transport:** Local File Browser Source

------------------------------------------------------------------------

## Use Cases

-   Esports broadcasts (live score, player stats)
-   Talk shows and podcasts (guest names, topics)
-   Conferences and panels (speaker automation)
-   Church streaming (sermons, verses, schedules)
-   Corporate webinars (lower thirds + live titles)
-   Branded live streams
-   Automated show packages and control rooms

------------------------------------------------------------------------

## Ecosystem

VinciFlow is part of the **streamrsc.com** production toolchain for
OBS Studio, providing:

-   Lower thirds
-   Scoreboards
-   Timers
-   Scene automation
-   Data-driven overlays
-   Broadcast-style UI tooling
-   OBS plugins and HTML overlay systems

------------------------------------------------------------------------

## Community & Support

-   Documentation: https://vinciflow.streamrsc.com/
-   Discord: https://discord.gg/2yD6B2PTuQ\
-   Facebook Group:
    https://www.facebook.com/groups/freestreamerspromotion\
-   Mastodon: https://mastodon.social/@streamrsc\
-   X (Twitter): https://x.com/streamcd_net\
-   Ko-fi: https://ko-fi.com/mmltech

------------------------------------------------------------------------

## License

GNU GPL v2.\
Commercial themes and premium extensions are distributed via
streamrsc.com.

------------------------------------------------------------------------

### Developed by MML Tech

Professional tools for real-time broadcast graphics.
