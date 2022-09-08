# Watchful

[![Build Status][status-badge]][status-result]

[status-badge]: https://github.com/pyrmont/watchful/workflows/build/badge.svg
[status-result]: https://github.com/pyrmont/watchful/actions?query=workflow%3Abuild

Watchful is a simple file system watching library written in C. It uses inotify
on Linux systems and FSEvents on macOS.

Watchful supports creating watches on directories. In addition, a user can set
a path to ignore (using [wildmatch][] patterns).

The library also includes a wrapper written in [Janet][].

[wildmatch]: https://github.com/davvid/wildmatch
[Janet]: https://janet-lang.org

## Requirements

Watchful is supported on Linux and macOS. Windows is not yet supported.

## Bugs

Found a bug? I'd love to know about it. The best way is to report your bug in
the [Issues][] section on GitHub.

[Issues]: https://github.com/pyrmont/watchful/issues

## Licence

Watchful is licensed under the MIT Licence. See [LICENSE][] for more details.

[LICENSE]: https://github.com/pyrmont/watchful/blob/master/LICENSE
