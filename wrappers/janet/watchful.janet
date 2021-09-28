(import ../../build/_watchful)


(defn monitor [path]
  (_watchful/monitor path (os/pipe)))


(defn start [monitor]
  (def pipe (_watchful/get-pipe monitor))
  (def channel (ev/chan 1))
  (defn watch []
    (forever
      (def pipe-open? (ev/read pipe 1))
      (unless pipe-open?
        (ev/chan-close channel)
        (break))
      (def event (_watchful/read-event pipe))
      (def chan-open? (ev/give channel event))
      (unless chan-open?
        (ev/close pipe)
        (break))))
  (ev/call watch)
  (_watchful/start monitor)
  channel)


(defn stop [monitor]
  (_watchful/stop monitor))


(defn watch [path on-event]
  (def pipe (os/pipe))
  (def monitor (_watchful/monitor path pipe))
  (def channel (start monitor))
  (defn clean-up []
    (ev/chan-close channel)
    (ev/close (get pipe 0))
    (stop monitor))
  (defn react []
    (defer (clean-up)
      (try
        (forever
          (def event (ev/take channel))
          (on-event event))
        ([err]
           (unless (= err "watch cancelled")
             (error "cannot read from channel"))))))
  (ev/call react))
