# Watchful API

## watchful

[create](#create), [watch](#watch)

## create

**cfunction**  | [source][1]

```janet
(watchful/create path &opt ignored-paths ignored-events backend)
```

Create a monitor for `path`

By default, Watchful will watch for creation, deletion, movement and modification events. In addition:

  - The user can provide an array/tuple of strings in `ignored-paths`.     If the path for an event includes an ignored path, the event is     ignored. Ignored paths are matched using the wildmatch library. This     allows `*` to match within path components and `**` to match     subdirectories.

  - The user can provide an array/tuple of keywords in `ignored-events`.     The events are `:created`, `:deleted`, `:moved` and `:modified`. If     the detected event is one of these events it will be ignored.

  - The user can specify the `backend` to use. The backend can be one of     `:fse` or `:inotify`. If the specified backend is not supported on     the host platform, the function will panic.

[1]: src/watchful.c#L383

## watch

**cfunction**  | [source][2]

```janet
(watchful/watch monitor on-event &opt options)
```

Watch `monitor` and call the function `on-event` on file system events

The watch uses a monitor created with Watchful's `create` function and an `on-event` callback. The `on-event` callback is a function that takes two arguments, `path` is the path of the file triggering the event and `event-types` is a tuple of the event types that occurred.

By default, the `watch` function does not terminate and will block the current thread. For this reason, in many cases the user will want to run the watch in a separate thread.

In addition, a user can specify the following `options`:

  - The `:count` option specifies the number of events to watch until     the watch finishes. If `:elapse` is also provided, the watch will     terminate when the first condition is met.

  - The `:elapse` option specifies the number of seconds to wait until     the watch finishes. If `:count` is also provided, the watch will     terminate when the first condition is met.

  - The `:freq` option specifies the minimum number of seconds that must     pass before the `on-event` callback is called. Events that occur     during the interval are dropped. By default, the frequency is set to    1. If the frequency is set to zero, no events will be dropped.

  - The `:on-ready` callback is a function that is called after the     watch begins. This can be used when the watch is run in a thread to     send a message to the parent thread.

[2]: src/watchful.c#L400

