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


(deftest is-watching
  (def path "tmp")
  (def monitor (watchful/monitor path))
  (is (not (watchful/watching? monitor)))
  (watchful/start monitor)
  (is (watchful/watching? monitor))
  (watchful/stop monitor)
  (is (not (watchful/watching? monitor))))


(deftest watch
  (def cwd (string (os/cwd) "/"))
  (def path "tmp")
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/touch path)
  (def result1 (ev/take channel))
  (os/touch path)
  (def result2 (ev/take channel))
  (watchful/cancel fiber)
  (def expect {:path (string cwd path "/") :type :modified})
  (is (= expect result1))
  (is (= expect result2)))


(deftest watch-with-ignored-paths
  (def cwd (string (os/cwd) "/"))
  (def path "tmp")
  (def ignored-file-1 (string path "/" (gensym) "ignored"))
  (def ignored-file-2 (string path "/" (gensym) "ignored"))
  (def noticed-file (string path "/" (gensym) "not-ignored"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f {:ignored-paths [ignored-file-1 ignored-file-2]}))
  (spit ignored-file-1 "")
  (spit ignored-file-2 "")
  (spit noticed-file "")
  (def event (ev/take channel))
  (watchful/cancel fiber)
  (is (= (string cwd noticed-file) (get event :path))))


(deftest watch-with-ignored-events
  (def cwd (string (os/cwd) "/"))
  (def path "tmp")
  (def created-file (string path "/" (gensym) "created"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f {:ignored-events [:modified]}))
  (os/touch path)
  (spit created-file "")
  (def event (ev/take channel))
  (watchful/cancel fiber)
  (is (= (string cwd created-file) (get event :path))))


(var reports nil)

(defer (rimraf "tmp")
  (os/mkdir "tmp")
  (set reports (run-tests! :exit-on-fail false)))

(unless (all (fn [x] (-> (get x :failures) length zero?)) reports)
  (os/exit 1))
