(import ../../build/_watchful)


(defn monitor [path pipe]
  (_watchful/monitor path pipe))


(defn start [monitor &opt pipe]
  (_watchful/start monitor)
  (if (nil? pipe)
    nil
    (do
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
      channel)))


(defn stop [monitor]
  (_watchful/stop monitor))


(defn watch [path on-event]
  (def [output input] (os/pipe))
  (def monitor (_watchful/monitor path input))
  (defn react []
    (defer (stop monitor)
      (try
        (forever
          (def pipe-open? (ev/read output 1))
          (unless pipe-open? (break))
          (def event (_watchful/read-event output))
          (on-event event))
        ([err]
           (unless (= err "watch cancelled")
             (pp err)
             (error "cannot read pipe"))))))
  (start monitor)
  (ev/call react))
