(import ../../build/_watchful)


(defn monitor [path &opt excluded_paths]
  (_watchful/monitor path excluded_paths))


(defn cancel [fiber]
  (ev/cancel fiber "watch cancelled"))


(defn start [monitor]
  (when (_watchful/watching? monitor) (error "monitor already watching"))
  (def pipe (os/pipe))
  (def output (get pipe 0))
  (def channel (ev/chan 1))
  (defn watch []
    (try
      (forever
        (def pipe-open? (ev/read output 1))
        (unless pipe-open?
          (ev/chan-close channel)
          (break))
        (def event (_watchful/read-event output))
        (def chan-open? (ev/give channel event))
        (unless chan-open?
          (ev/close output)
          (break)))
      ([err]
       (unless (= err "stream is closed")
         (error "cannot read from stream")))))
  (ev/call watch)
  (_watchful/start monitor pipe)
  channel)


(defn stop [monitor]
  (unless (_watchful/watching? monitor) (error "monitor already stopped"))
  (def pipe (_watchful/get-pipe monitor))
  (ev/close pipe)
  (_watchful/stop monitor))


(defn watch [path on-event]
  (def monitor (_watchful/monitor path nil))
  (def channel (start monitor))
  (defn clean-up []
    (ev/chan-close channel)
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


(defn watching? [monitor]
  (_watchful/watching? monitor))
