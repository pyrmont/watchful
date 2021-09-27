(import testament :prefix "" :exit true)
(import ../wrappers/janet/watchful)

(defn- rimraf
  [path]
  (if-let [m (os/stat path :mode)]
    (if (= m :directory)
      (do
        (each subpath (os/dir path) (rimraf (string path "/" subpath)))
        (os/rmdir path))
      (os/rm path))))


(deftest watch
  (def path "tmp")
  (def full-path (string (os/cwd) "/" path "/"))
  (def channel (ev/chan 1))
  (defn f [e]
    (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/execute ["touch" path] :p)
  (def result (ev/take channel))
  (ev/cancel fiber "watch cancelled")
  (is (== full-path result)))


# (deftest watch-with-timeout
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (def start (os/mktime (os/date)))
#   (watchful/create "test")
#   # (watchful/watch (watchful/create dir)
#   #                 (fn [path event-types] nil)
#   #                 {:elapse 1.0})
#   (def end (os/mktime (os/date)))
#   (is true))
  # (is (>= (- end start) 1)))


# (deftest watch-with-count
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (defn worker [parent]
#     (def output @"")
#     (def monitor (watchful/create dir))
#     (watchful/watch monitor
#                     (fn [path event-types] (buffer/push output "Detected"))
#                     {:count 1
#                      :elapse 2.0
#                      :on-ready (fn [] (thread/send parent :ready))})
#     (thread/send parent output))
#   (thread/new worker)
#   (when (= :ready (thread/receive 5))
#     (os/touch dir)
#     (def result (thread/receive math/inf))
#     (is (== "Detected" result))))


# (deftest watch-with-ignored-paths
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (defn worker [parent]
#     (def output @"")
#     (def monitor (watchful/create dir ["foo.txt"]))
#     (watchful/watch monitor
#                     (fn [path event-types] (buffer/push output "Detected"))
#                     {:elapse 2.0
#                      :on-ready (fn [] (thread/send parent :ready))})
#     (thread/send parent output))
#   (thread/new worker)
#   (when (= :ready (thread/receive 5))
#     (spit (string dir "/foo.txt") "Hello world")
#     (def result (thread/receive math/inf))
#     (is (== "" result))))


# (deftest watch-with-ignored-events
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (defn worker [parent]
#     (def output @"")
#     (def monitor (watchful/create dir [] [:created :modified]))
#     (watchful/watch monitor
#                     (fn [path event-types] (buffer/push output "Detected"))
#                     {:elapse 2.0
#                      :on-ready (fn [] (thread/send parent :ready))})
#     (thread/send parent output))
#   (thread/new worker)
#   (when (= :ready (thread/receive 5))
#     (spit (string dir "/foo.txt") "Hello world")
#     (def result (thread/receive math/inf))
#     (is (== "" result))))


# (deftest watch-with-frequency
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (defn worker [parent]
#     (def output @"")
#     (def monitor (watchful/create dir []))
#     (watchful/watch monitor
#                     (fn [path event-types] (buffer/push output "Detected"))
#                     {:elapse 2.0
#                      :freq 5.0
#                      :on-ready (fn [] (thread/send parent :ready))})
#     (thread/send parent output))
#   (thread/new worker)
#   (when (= :ready (thread/receive 5))
#     (spit (string dir "/foo.txt") "Hello world")
#     (spit (string dir "/bar.txt") "Hello world")
#     (def result (thread/receive math/inf))
#     (is (== "Detected" result))))


# (deftest watch-with-no-frequency
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (defn worker [parent]
#     (def output @"")
#     (def monitor (watchful/create dir []))
#     (watchful/watch monitor
#                     (fn [path event-types] (buffer/push output "Detected"))
#                     {:count 2
#                      :freq 0.0
#                      :on-ready (fn [] (thread/send parent :ready))})
#     (thread/send parent output))
#   (thread/new worker)
#   (when (= :ready (thread/receive 5))
#     (spit (string dir "/foo.txt") "Hello world")
#     (spit (string dir "/bar.txt") "Hello world")
#     (def result (thread/receive 10))
#     (is (== "DetectedDetected" result))))


# (deftest watch-with-invalid-args
#   (def dir (string "tmp/" (gensym)))
#   (os/mkdir dir)
#   (def monitor (watchful/create dir []))
#   (def no-op (fn [path event-types] nil))
#   (is (thrown? "value for :count must be a positive number" (watchful/watch monitor no-op {:count "hello"})))
#   (is (thrown? "value for :elapse must be a positive number" (watchful/watch monitor no-op {:elapse "hello"})))
#   (is (thrown? "value for :freq must be a positive number" (watchful/watch monitor no-op {:freq "hello"})))
#   (is (thrown? "value for :freq must be a positive number" (watchful/watch monitor no-op {:freq -1})))
#   (is (thrown? "value for :on-ready must be a function" (watchful/watch monitor no-op {:on-ready "hello"}))))

(run-tests!)

# (defer (rimraf "tmp")
#   (os/mkdir "tmp")
#   (run-tests! :exit-on-fail false))
