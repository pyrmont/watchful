# Watchful

[![Build Status][status-badge]][status-result]

[status-badge]: https://github.com/pyrmont/watchful/workflows/build/badge.svg
[status-result]: https://github.com/pyrmont/watchful/actions?query=workflow%3Abuild

Watchful is a file-watching module for Janet. It uses inotify on Linux systems
and FSEvents on macOS.

Watchful supports watches on files and directories (including subdirectories).
In addition, a user can set:

- paths to ignore (using [wildmatch][] patterns);
- a termination condition after a set number of events;
- a termination condition after a set duration;
- a minimum delay between event detection (default is 1 second); and
- a function to call once the watch begins.

[wildmatch]: https://github.com/davvid/wildmatch

## Requirements

Watchful is supported on Linux and macOS. Windows is not yet supported.

## Installation

Add the dependency to your `project.janet` file:

```
(declare-project
  :dependencies ["https://github.com/pyrmont/watchful"])
```

## Usage

Watchful can be used like this:

```
(import watchful)

(def monitor (watchful/create "path-to-watch" ["pattern-to-exclude"]))

(defn cb [path event-types] (print "File change detected"))

(watchful/watch monitor cb {:elapse 1.0}) # watch will terminate after 1 second
```

## API

Documentation for Watchful's API is in [api.md][api]

[api]: https://github.com/pyrmont/watchful/blob/master/api.md

## Bugs

Found a bug? I'd love to know about it. The best way is to report your bug in
the [Issues][] section on GitHub.

[Issues]: https://github.com/pyrmont/watchful/issues

## Licence

Watchful is licensed under the MIT Licence. See [LICENSE][] for more details.

[LICENSE]: https://github.com/pyrmont/watchful/blob/master/LICENSE
