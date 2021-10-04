(declare-project
  :name "Watchful"
  :description "A file system watching utility"
  :author "Michael Camilleri"
  :license "MIT"
  :url "https://github.com/pyrmont/watchful"
  :repo "git+https://github.com/pyrmont/watchful"
  :dependencies ["https://github.com/pyrmont/testament"])


(def cflags
  ["-Wno-unused-parameter"])


(def platform-cflags
  (case (os/which)
   :macos ["-DMACOS=1" "-Wno-unused-command-line-argument"]
   :linux ["-DLINUX=1" "-pthread"]
   []))


(def lflags
  [])


(def platform-lflags
  (case (os/which)
   :macos["-framework" "CoreFoundation" "-framework" "CoreServices"]
   []))


(declare-native
  :name "_watchful"
  :cflags [;default-cflags ;cflags ;platform-cflags]
  :lflags [;default-lflags ;lflags ;platform-lflags]
  :headers @["src/watchful.h"
             "wrappers/janet/wrapper.h"]
  :source @["src/backends/fsevents.c"
            "src/backends/inotify.c"
            "src/wildmatch.c"
            "src/watchful.c"
            "wrappers/janet/functions.c"
            "wrappers/janet/monitor.c"])


(declare-source
  :source ["wrappers/janet/watchful.janet"])
