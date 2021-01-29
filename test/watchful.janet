(import testament :prefix "" :exit true)
(import ../build/watchful :as watchful)


(defn- rimraf
  [path]
  (if-let [m (os/stat path :mode)]
    (if (= m :directory)
      (do
        (each subpath (os/dir path) (rimraf (string path "/" subpath)))
        (os/rmdir path))
      (os/rm path))))


(deftest watch-with-timeout
  (def dir (string "tmp/" (gensym)))
  (os/mkdir dir)
  (def start (os/mktime (os/date)))
  (watchful/watch (watchful/create dir)
                  (fn [path event-types] nil)
                  [:elapse 1.0])
  (def end (os/mktime (os/date)))
  (is (>= (- end start) 1)))


(deftest watch-with-count
  (def dir (string "tmp/" (gensym)))
  (os/mkdir dir)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create dir))
    (watchful/watch monitor
                    (fn [path event-types] (buffer/push output "Detected"))
                    [:count 1 :elapse 2.0]
                    (fn [] (thread/send parent :ready)))
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (os/touch dir)
    (def result (thread/receive math/inf))
    (is (== "Detected" result))))


(deftest watch-with-ignored-paths
  (def dir (string "tmp/" (gensym)))
  (os/mkdir dir)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create dir ["foo.txt"]))
    (watchful/watch monitor
                    (fn [path event-types] (buffer/push output "Detected"))
                    [:elapse 1.0]
                    (fn [] (thread/send parent :ready)))
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (spit (string dir "/foo.txt") "Hello world")
    (def result (thread/receive math/inf))
    (is (== "" result))))


(deftest watch-with-ignored-events
  (def dir (string "tmp/" (gensym)))
  (os/mkdir dir)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create dir [] [:created :modified]))
    (watchful/watch monitor
                    (fn [path event-types] (buffer/push output "Detected"))
                    [:elapse 1.0]
                    (fn [] (thread/send parent :ready)))
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (spit (string dir "/foo.txt") "Hello world")
    (def result (thread/receive math/inf))
    (is (== "" result))))


(defer (rimraf "tmp")
  (os/mkdir "tmp")
  (run-tests! :exit-on-fail false))
