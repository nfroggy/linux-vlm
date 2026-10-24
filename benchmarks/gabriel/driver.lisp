;;; -*- Mode: Lisp; Package: GABRIEL; Syntax: Common-Lisp -*-
;;; Measurement driver for the unmodified Symbolics Gabriel benchmark suite.
(in-package "GABRIEL")

(defun vlm-ascii-row (path if-exists text)
  ;; Binary output avoids Genera's character-style escape sequences on append.
  (with-open-file (s path :direction :output :if-exists if-exists
                         :if-does-not-exist :create
                         :element-type '(unsigned-byte 8))
    (map nil #'(lambda (ch)
                 (write-byte (if (char= ch #\Newline) 10 (char-code ch)) s))
         text)))

(defun vlm-csv-row (path name sample calls real run)
  ;; Close between samples: network I/O is outside the measured interval.
  (vlm-ascii-row path :append
    (format nil "~A,~D,~D,~D,~D,~D~%" (symbol-name name) sample calls
            (round (* real internal-time-units-per-second))
            (round (* run internal-time-units-per-second))
            internal-time-units-per-second)))

(defun vlm-benchmarks (path &optional (samples 1) counts)
  (si:gc-on :ephemeral t :dynamic nil)
  (vlm-ascii-row path :supersede
    (format nil "benchmark,sample,calls,real_ticks,run_ticks,ticks_per_second~%"))
  (with-open-file (s (concatenate 'string path ".values") :direction :output
                    :if-exists :supersede)
    (format s "Warm-up return values, outside measured intervals.~%"))
  (dolist (name *series*)
    (let ((fn (get name 'timing-function))
          (calls (or (cdr (assoc name counts)) 1)))
      ;; First call warms the code and data, and initializes TRAVERSE's graph.
      (let ((value (funcall fn)))
        (with-open-file (s (concatenate 'string path ".values") :direction :output
                          :if-exists :append)
          (let ((*print-pretty* nil) (*print-length* nil) (*print-level* nil))
            (format s "~A ~S~%" (symbol-name name) value))))
      (dotimes (sample samples)
        (multiple-value-bind (real run)
            (timed-duration #'(lambda () (dotimes (i calls) (funcall fn))))
          (vlm-csv-row path name (1+ sample) calls real run)))
      (format t "~&Finished ~A (~D calls/sample).~%" name calls)))
  (with-open-file (s (concatenate 'string path ".done") :direction :output
                    :if-exists :supersede)
    (format s "Completed ~D benchmarks, ~D samples each.~%" (length *series*) samples))
  (format t "~&GABRIEL BENCHMARKS COMPLETE.~%"))
