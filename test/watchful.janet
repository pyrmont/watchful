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
  (def path "tmp")
  (def full-path (string (os/cwd) "/" path "/"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f))
  (os/execute ["touch" path] :p)
  (def result (ev/take channel))
  (watchful/cancel fiber)
  (def expect {:path full-path :type :modified})
  (is (== expect result)))


(deftest watch-with-ignored-path
  (def path "tmp")
  (def full-path (string (os/cwd) "/" path "/"))
  (def channel (ev/chan 1))
  (defn f [e] (ev/give channel e))
  (def fiber (watchful/watch path f {:ignored-paths ["tmp/ignored"]}))
  (os/execute ["touch" (string path "/ignored")] :p)
  (os/execute ["touch" (string path "/not-ignored")] :p)
  (def event (ev/take channel))
  (watchful/cancel fiber)
  (is (= (string full-path "not-ignored") (get event :path))))


(var reports nil)

(defer (rimraf "tmp")
  (os/mkdir "tmp")
  (set reports (run-tests! :exit-on-fail false)))

(unless (all (fn [x] (-> (get x :failures) length zero?)) reports)
  (os/exit 1))
