(import testament :prefix "" :exit true)
(import ../build/watchful :as watchful)

# (deftest watch-with-no-path
#   (is (= true true)))


# (run-tests!)

(def x 5)

(defn cb [] (print "The number is " x))

(def path (watchful/create :fse "test"))

(watchful/watch [path :count 1]
                (fn [] (print "Hello world!")))
