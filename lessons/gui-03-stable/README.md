# GUI-03: Fixed keyboard and mouse input events

> **Course status: learning checkpoint.**

A bounded input event queue models keyboard, mouse, and timer events without dynamic allocation. PS/2 scan-code conversion and three-byte mouse packet metadata remain deterministic and safely report unavailable devices.

Commands: `inputtest`, plus `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.
