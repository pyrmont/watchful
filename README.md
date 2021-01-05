# Watchful

[![Build Status][status-badge]][status-result]

[status-badge]: https://github.com/pyrmont/watchful/workflows/build/badge.svg
[status-result]: https://github.com/pyrmont/watchful/actions?query=workflow%3Abuild

Watchful is a file-watching module for Janet. It uses inotify on Linux systems
and FSEvents on macOS.

## Requirements

Watchful is supported on Linux and macOS. Windows is not yet supported.

## Installation

Add the dependency to your `project.janet` file:

```
(declare-project
  :dependencies ["https://github.com/pyrmont/watchful"])
```

Then run:

```shell
$ jpm deps
```

## Usage

Watchful provides two functions:

- `(create path &opt excludes)`: This function creates a monitor object. The
  monitor holds a string representing the path to watch. The path can be
  relative to the current working directory or an absolute directory. The
  monitor can also hold a tuple of strings representing paths to exclude.

- `(watch monitor cb &opt options)`: This function uses the monitor to watch a
  path with a callback that will be called when changes in the watched
  directory are detected.

  Two options are currently supported, `:count` and `:elapse`. If `:count` is
  provided, the watch will end after that number of changes is detected. If
  `:elapse` is provided, the watch will end after that number of seconds. Both
  `:count` and `:elapse` can be provided.


It can be used like this:

```
(import watchful)

(def monitor (watchful/create "path-to-watch" ["file-to-exclude"]))

(defn cb [path event-type] (print "File change detected"))

(watchful/watch monitor cb [:elapse 1.0])
```

## Bugs

Found a bug? I'd love to know about it. The best way is to report your bug in
the [Issues][] section on GitHub.

[Issues]: https://github.com/pyrmont/watchful/issues

## Licence

Watchful is licensed under the MIT Licence. See [LICENSE][] for more details.

[LICENSE]: https://github.com/pyrmont/watchful/blob/master/LICENSE
