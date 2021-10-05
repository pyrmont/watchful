(import _watchful)


(defn monitor [path &opt opts]
  (_watchful/monitor path opts))


(defn cancel [fiber]
  (ev/cancel fiber "watch cancelled"))


(defn start [monitor]
  (when (_watchful/watching? monitor) (error "monitor already watching"))
  (def signals (ev/chan 1))
  (def events (ev/chan 1))
  (defn supervise []
    (def [status fiber] (ev/take signals))
    (when (= status :error)
      (def error-value (fiber/last-value fiber))
      (if (= error-value "stream is closed")
        (ev/chan-close events)
        (propagate error-value fiber))))
  (ev/call supervise)
  (def pipe (os/pipe))
  (def output (get pipe 0))
  (defn read-path []
    (var path-length 0)
    (while (def byte (first (ev/read output 1)))
      (if (zero? byte)
        (break)
        (+= path-length byte)))
    (if (zero? path-length)
      nil
      (string (ev/chunk output path-length))))
  (defn create-event [event-type]
    (if (= :renamed event-type)
      (if-let [new-path (read-path)
               old-path (read-path)]
        {:type event-type :path new-path :old-path old-path})
      {:type event-type :path (read-path)}))
  (defn receive []
    (forever
      (def event-type
        (case (first (ev/read output 1))
          1 :modified
          2 :created
          4 :deleted
          8 :renamed))
      (def event (create-event event-type))
      (when (nil? event)
        (ev/chan-close events)
        (break))
      (def chan-open? (ev/give events event))
      (unless chan-open?
        (ev/close output)
        (break))))
  (ev/go (fiber/new receive :t) nil signals)
  (_watchful/start monitor pipe)
  events)


(defn stop [monitor]
  (unless (_watchful/watching? monitor) (error "monitor already stopped"))
  (def pipe (_watchful/get-pipe monitor))
  (ev/close pipe)
  (_watchful/stop monitor))


(defn watch [path on-event &opt opts]
  (def monitor (_watchful/monitor path opts))
  (def signals (ev/chan 1))
  (def events (start monitor))
  (defn clean-up []
    (ev/chan-close events)
    (stop monitor))
  (defn supervise []
    (def [status fiber] (ev/take signals))
    (when (= status :error)
      (def error-value (fiber/last-value fiber))
      (if (= error-value "watch cancelled")
        (clean-up)
        (propagate error-value fiber))))
  (ev/call supervise)
  (defn react []
    (forever
      (def event (ev/take events))
      (cond
        (nil? event)
        (break)

        (and (tuple? event)
             (= :close (first event)))
        (error "stream closed unexpectedly")

        (on-event event))))
  (ev/go (fiber/new react :t) nil signals))


(defn watching? [monitor]
  (_watchful/watching? monitor))
