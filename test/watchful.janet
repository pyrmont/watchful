(import testament :prefix "" :exit true)
(import ../wrappers/janet/watchful)


(def tmp-root "tmp/")
(def cwd (string (os/cwd) "/"))


(defn- mkdir-p [path]
  (->> (string/split "/" path)
       (reduce (fn [acc x]
                 (def curr-path (string acc x "/"))
                 (os/mkdir curr-path)
                 curr-path)
               (if (string/has-prefix? "/" path)
                 "/"
                 ""))))


(defn- rimraf [path]
  (if-let [m (os/stat path :mode)]
    (if (= m :directory)
      (do
        (each subpath (os/dir path) (rimraf (string path "/" subpath)))
        (os/rmdir path))
      (os/rm path))))


(defn- tmp-dir []
  (def path (string tmp-root (gensym) "/"))
  (os/mkdir path)
  path)


(deftest is-watching
  (def path (tmp-dir))
  (def monitor (watchful/monitor path))
  (is (not (watchful/watching? monitor)))
  (watchful/start monitor)
  (is (watchful/watching? monitor))
  (watchful/stop monitor)
  (is (not (watchful/watching? monitor))))


(deftest watch
  (def path (tmp-dir))
  (def channel (ev/chan))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/touch path)
  (def event-1 (ev/take channel))
  (def expect-1 {:type :modified :at (event-1 :at) :path (string cwd path)})
  (is (= expect-1 event-1))
  (os/touch path)
  (def event-2 (ev/take channel))
  (def expect-2 {:type :modified :at (event-2 :at) :path (string cwd path)})
  (is (= expect-2 event-2))
  (watchful/cancel fiber))


(deftest watch-with-ignored-paths
  (def path (tmp-dir))
  (def ignored-file-1 (string path (gensym) "ignored"))
  (def ignored-file-2 (string path (gensym) "ignored"))
  (def noticed-file (string path (gensym) "not-ignored"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f nil {:ignored-paths [ignored-file-1 ignored-file-2]}))
  (spit ignored-file-1 "")
  (spit ignored-file-2 "")
  (spit noticed-file "")
  (def event (ev/take channel))
  (is (= (string cwd noticed-file) (get event :path)))
  (watchful/cancel fiber))


(deftest watch-with-ignored-events
  (def path (tmp-dir))
  (def created-file (string path (gensym) "created"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f nil {:ignored-events [:modified]}))
  (os/touch path)
  (spit created-file "")
  (def event (ev/take channel))
  (is (= (string cwd created-file) (get event :path)))
  (watchful/cancel fiber))


(deftest watch-with-moved-file
  (def path (tmp-dir))
  (def before-file (string path (gensym) "before"))
  (def after-file (string path (gensym) "after"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (spit before-file "")
  (os/rename before-file after-file)
  (unless (= :macos (os/which))
    (def event-1 (ev/take channel))
    (def expect-1 {:type :created :at (event-1 :at) :path (string cwd before-file)})
    (is (= expect-1 event-1)))
  (def event-2 (ev/take channel))
  (def expect-2 {:type :renamed :at (event-2 :at) :path (string cwd after-file) :old-path (string cwd before-file)})
  (is (= expect-2 event-2))
  (watchful/cancel fiber))


(deftest watch-with-deleted-file
  (def path (tmp-dir))
  (def deleted-file (string path (gensym) "deleted"))
  (spit deleted-file "")
  (def deleted-dir (string path (gensym) "deleted/"))
  (os/mkdir deleted-dir)
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/rm deleted-file)
  (def event-1 (ev/take channel))
  (def expect-1 {:type :deleted :at (event-1 :at) :path (string cwd deleted-file)})
  (is (= expect-1 event-1))
  (os/rmdir deleted-dir)
  (def event-2 (ev/take channel))
  (def expect-2 {:type :deleted :at (event-2 :at) :path (string cwd deleted-dir)})
  (is (= expect-2 event-2))
  (watchful/cancel fiber))


(deftest watch-with-nested-dirs
  (def path (tmp-dir))
  (def before-parent (string path "before/"))
  (os/mkdir before-parent)
  (def after-parent (string path "after/"))
  (os/mkdir after-parent)
  (def before-nested-dir (string before-parent "moved/"))
  (os/mkdir before-nested-dir)
  (def after-nested-dir (string after-parent "moved/"))
  (def created-file (string after-nested-dir (gensym) "created"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/rename before-nested-dir after-nested-dir)
  (def event-1 (ev/take channel))
  (def expect-1 {:type :renamed :at (event-1 :at) :path (string cwd after-nested-dir) :old-path (string cwd before-nested-dir)})
  (is (= expect-1 event-1))
  (spit created-file "")
  (def event-2 (ev/take channel))
  (def expect-2 {:type :created :at (event-2 :at) :path (string cwd created-file)})
  (is (= expect-2 event-2))
  (watchful/cancel fiber))


(var reports nil)

(defer (rimraf tmp-root)
  (os/mkdir tmp-root)
  (set reports (run-tests! :exit-on-fail false)))

(unless (all (fn [x] (-> (get x :failures) length zero?)) reports)
  (os/exit 1))
