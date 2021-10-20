(import _watchful)


(defn monitor [path &opt opts]
  (_watchful/monitor path opts))


(defn cancel [fiber]
  (ev/cancel fiber "watch cancelled"))


(defn start [monitor]
  (when (_watchful/watching? monitor) (error "monitor already watching"))
  (def events (ev/thread-chan 10))
  (defn give [event]
    (protect (ev/give events event)))
  (_watchful/start monitor give)
  events)


(defn stop [monitor]
  (unless (_watchful/watching? monitor) (error "monitor already stopped"))
  (_watchful/stop monitor))


(defn watch [path on-event &opt on-cancel opts]
  (def monitor (_watchful/monitor path opts))
  (def signals (ev/chan))
  (def events (start monitor))
  (defn clean-up []
    (unless (nil? on-cancel)
      (on-cancel))
    (stop monitor)
    (ev/give events nil))
    # (ev/chan-close events))
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
