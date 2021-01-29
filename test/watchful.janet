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
  (print "\nwatch-with-timeout")
  (os/mkdir "tmp/tmp1")
  (def start (os/mktime (os/date)))
  (watchful/watch (watchful/create "tmp/tmp1")
                  (fn [path event-type] nil)
                  [:elapse 1.0])
  (def end (os/mktime (os/date)))
  (is (>= (- end start) 1)))


(deftest watch-with-count
  (print "\nwatch-with-count")
  (os/mkdir "tmp/tmp2")
  (os/sleep 1)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create "tmp/tmp2"))
    (thread/send parent :ready)
    (watchful/watch monitor
                    (fn [path event-type] (buffer/push output "Detected"))
                    [:count 1 :elapse 2.0])
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (os/touch "tmp/tmp2")
    (def result (thread/receive math/inf))
    (is (== "Detected" result))))


(deftest watch-with-ignored-paths
  (print "\nwatch-with-ignored-paths")
  (os/mkdir "tmp/tmp3")
  (os/sleep 1)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create "tmp/tmp3" ["foo.txt"]))
    (thread/send parent :ready)
    (watchful/watch monitor
                    (fn [path event-type] (buffer/push output "Detected"))
                    [:elapse 2.0])
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (spit "tmp/tmp3/foo.txt" "Hello world")
    (def result (thread/receive math/inf))
    (is (== "" result))))


(deftest watch-with-ignored-events
  (print "\nwatch-with-ignored-events")
  (os/mkdir "tmp/tmp4")
  (os/sleep 1)
  (defn worker [parent]
    (def output @"")
    (def monitor (watchful/create "tmp/tmp4" [] [:created :modified]))
    (thread/send parent :ready)
    (watchful/watch monitor
                    (fn [path event-type] (buffer/push output "Detected"))
                    [:elapse 2.0])
    (thread/send parent output))
  (thread/new worker)
  (when (= :ready (thread/receive 5))
    (spit "tmp/tmp4/foo.txt" "Hello world")
    (def result (thread/receive math/inf))
    (is (== "" result))))


(defer (rimraf "tmp")
  (os/mkdir "tmp")
  (run-tests!))
