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
  (def event-1 (ev/take channel))
  (os/touch path)
  (def event-2 (ev/take channel))
  (watchful/cancel fiber)
  (def expect {:path (string cwd path "/") :type :modified})
  (is (= expect event-1))
  (is (= expect event-2)))


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


(deftest watch-with-moved-file
  (def cwd (string (os/cwd) "/"))
  (def path "tmp")
  (def before-file (string path "/" (gensym) "before"))
  (def after-file (string path "/" (gensym) "after"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (spit before-file "")
  (os/rename before-file after-file)
  (def event-1 (ev/take channel))
  (def event-2 (ev/take channel))
  (watchful/cancel fiber)
  (def expect-1 {:type :created :path (string cwd before-file)})
  (def expect-2 {:type :renamed :path (string cwd after-file) :old-path (string cwd before-file)})
  (is (= expect-1 event-1))
  (is (= expect-2 event-2)))


(var reports nil)

(defer (rimraf "tmp")
  (os/mkdir "tmp")
  (set reports (run-tests! :exit-on-fail false)))

(unless (all (fn [x] (-> (get x :failures) length zero?)) reports)
  (os/exit 1))
