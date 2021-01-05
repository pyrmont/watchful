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
  (os/mkdir "tmp")
  (defer (rimraf "tmp")
    (def start (os/mktime (os/date)))
    (watchful/watch (watchful/create "tmp")
                    (fn [path event-type] nil)
                    [:elapse 1.0])
    (def end (os/mktime (os/date)))
    (is (>= (- end start) 1))))


(deftest watch-with-count
  (os/mkdir "tmp")
  (defer (rimraf "tmp")
    (defn worker [parent]
      (def output @"")
      (watchful/watch (watchful/create "tmp")
                      (fn [path event-type] (buffer/push output "Detected"))
                      [:count 1 :elapse 5.0])
      (thread/send parent output))
    (thread/new worker)
    (os/sleep 1)
    (os/touch "tmp")
    (def result (thread/receive))
    (is (== result "Detected"))))


(deftest watch-with-exclusions
  (os/mkdir "tmp")
  (defer (rimraf "tmp")
    (defn worker [parent]
      (def output @"")
      (watchful/watch (watchful/create "tmp" ["foo.txt"])
                      (fn [path event-type] (buffer/push output "Detected"))
                      [:elapse 1.0])
      (thread/send parent output))
    (thread/new worker)
    (os/sleep 1)
    (spit "tmp/foo.txt" "Hello world")
    (def result (thread/receive))
    (is (== result ""))))


(run-tests!)
