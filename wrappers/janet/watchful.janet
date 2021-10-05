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
  (defn receive []
    (forever
      (var path-length 0)
      (while (def byte (first (ev/read output 1)))
        (if (zero? byte)
          (break)
          (+= path-length byte)))
      (when (zero? path-length)
        (ev/chan-close events)
        (break))
      (def event-path (string (ev/chunk output path-length)))
      (def event-type
        (case (first (ev/read output 1))
          1 :created
          2 :deleted
          4 :moved
          8 :modified))
      (def event {:path event-path :type event-type})
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
