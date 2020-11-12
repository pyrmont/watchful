(import testament :prefix "" :exit true)
(import ../build/watchful :as watchful)

# (deftest watch-with-no-path
#   (is (= true true)))


# (run-tests!)

(def x 5)

(defn cb [] (print "The number is " x))

(def monitor (watchful/create :fse cb "test"))

(watchful/watch monitor)

(os/sleep 5)

(watchful/destroy monitor)
