(import testament :prefix "" :exit true)
(import ../build/watchful :as watchful)

# (deftest watch-with-no-path
#   (is (= true true)))


# (run-tests!)

(def x 5)

(defn cb [path event-type] (print "The changed path is " path))

(def path (watchful/create "test" ["hello.txt"]))

(watchful/watch [path :count 1 :elapse 5.0] cb)
