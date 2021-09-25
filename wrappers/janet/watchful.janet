(import ../../build/_watchful)


(defn watch [path channel]
  (def [output input] (os/pipe))
  (def monitor (_watchful/monitor path input))
  (_watchful/start monitor)
  (_watchful/stop monitor)
  # (_watchful/start monitor)
  # (forever
  #   (def len (ev/read reader 8))
  #   (when (nil? len) (break))
  #   (def bytes (ev/read reader len))
  #   (ev/give channel (_watchful/make-event bytes)))
  (ev/chan-close channel))
