(declare-project
  :name "Watchful"
  :description "A file system watching utility"
  :author "Michael Camilleri"
  :license "MIT"
  :url "https://github.com/pyrmont/watchful"
  :repo "git+https://github.com/pyrmont/watchful"
  :dependencies ["https://github.com/pyrmont/testament"])


(def cflags
  ["-Ifsmon" "-Ifsmon/backend" "-DFSMON_VERSION=\"1.8.1\""])


(def platform-cflags
  (case (os/which)
   :macos
   ["-mmacosx-version-min=10.12" "-DMACOS=1" "-framework" "CoreServices" "-Wno-unused-parameter" "-Wno-unused-command-line-argument"]

   :linux
   ["-DLINUX=1" "-pthread" "-Wno-unused-parameter"]

   ["-Wno-unused-parameter"]))


(def lflags
  [])


(def platform-lflags
  [])


(declare-native
  :name "watchful"
  :cflags [;default-cflags ;cflags ;platform-cflags]
  :lflags [;default-lflags ;lflags ;platform-lflags]
  :headers @["src/watchful.h"]
  :source @["src/watchful/fse.c"
            "src/watchful/inotify.c"
            "src/watchful/wildmatch.c"
            "src/watchful.c"])
