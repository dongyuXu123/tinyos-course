# Lesson 60: controlled user-space job/session model

> **Course status: learning checkpoint.**

A fixed init/shell session owns at most two child jobs and composes argv/env, descriptors, pipes, signals, timers, deferred work, wait/reap, and resource teardown metadata without executing arbitrary user code.

Commands: `jobtest`, `sessioninfo`, plus all Lesson 59 regressions.
