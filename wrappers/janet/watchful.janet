(import ../../build/_watchful)


(defn monitor [path &opt excluded_paths]
  (_watchful/monitor path excluded_paths))


(defn cancel [fiber]
  (ev/cancel fiber "watch cancelled"))


(defn start [monitor]
  (when (_watchful/watching? monitor) (error "monitor already watching"))
  (def pipe (os/pipe))
  (def output (get pipe 0))
  (def events (ev/chan 1))
  (def signals (ev/chan 1))
  (defn supervise []
    (def [status fiber] (ev/take signals))
    (when (= status :error)
      (def error-value (fiber/last-value fiber))
      (if (= error-value "stream is closed")
        (ev/chan-close events)
        (propagate error-value fiber))))
  (ev/call supervise)
  (defn watch []
    (forever
      (def pipe-open? (ev/read output 1))
      (unless pipe-open?
        (ev/chan-close events)
        (break))
      (def event (_watchful/read-event output))
      (def chan-open? (ev/give events event))
      (unless chan-open?
        (ev/close output)
        (break))))
  (ev/go (fiber/new watch :t) nil signals)
  (_watchful/start monitor pipe)
  events)


(defn stop [monitor]
  (unless (_watchful/watching? monitor) (error "monitor already stopped"))
  (def pipe (_watchful/get-pipe monitor))
  (ev/close pipe)
  (_watchful/stop monitor))


(defn watch [path on-event &opt excluded-paths]
  (def monitor (_watchful/monitor path excluded-paths))
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
      (on-event (ev/take events))))
  (ev/go (fiber/new react :t) nil signals))


(defn watching? [monitor]
  (_watchful/watching? monitor))
