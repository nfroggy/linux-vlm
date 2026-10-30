;;; -*- Mode: LISP; Syntax: Common-Lisp; Package: X-SCREEN; Base: 10 -*-
;;; Load after OG2-patches/xorg-keyboard-layout.lisp in a disposable Genera
;;; Xorg/Xvfb console.  This exercises the real console and layout APIs.

(in-package "X-SCREEN")

(defun test-vlm-xorg-keyboard ()
  (let* ((console sys:*console*)
         (keyboard (sys:console-keyboard console))
         (display (x-console-display console))
         (offset (x-console-keycode-offset console))
         (table (sys:keyboard-keyboard-table keyboard))
         (initial-table (sys:copy-keyboard-table table))
         (name (sys:keyboard-layout-type keyboard))
         (record (vlm-xorg-layout-record name))
         (layout (system-applications::find-keyboard-layout name))
         (backspace (- (vlm-xorg-keycode display #xff08) offset))
         (delete (- (vlm-xorg-keycode display #xffff) offset))
         (saved-remappings (copy-tree cli::*keyboard-table-remappings*))
         (defaults (system-applications::keyboard-layout-default-mapping-table layout))
         (saved-defaults (sys:copy-keyboard-table defaults)))
    (assert record)
    (assert (equal (array-dimensions table) (array-dimensions defaults)))
    (flet ((mapped-p (code character)
             (loop for shift below (sys:keyboard-table-shift-index-limit table)
                   always (eql (aref table shift code)
                               (sys:standardize-keyboard-mapping character t)))))
      (assert (mapped-p backspace #\Rubout))
      (assert (mapped-p delete #\Backspace))
      (assert (string-equal
                (system-applications::keyboard-layout-key-name
                  (system-applications::keyboard-layout-key-for-code layout backspace))
                "Backspace"))
      (assert (string-equal
                (system-applications::keyboard-layout-key-name
                  (system-applications::keyboard-layout-key-for-code layout delete))
                "Delete"))
      ;; An X mapping refresh must retain both the swap and the layout name.
      (x-console-update-keyboard-mapping console)
      (assert (eq name (sys:keyboard-layout-type keyboard)))
      (assert (mapped-p backspace #\Rubout))
      (assert (mapped-p delete #\Backspace))
      (unwind-protect
          (progn
            ;; Use the same API emitted by Keyboard Control's Save Differences.
            (sys:set-keyboard-table-mapping #\Help name backspace :all-shifts t)
            (x-console-update-keyboard-mapping console)
            (assert (mapped-p backspace #\Help))
            ;; Reconnect may redetect the original profile before the hook runs.
            (console-set-keyboard-type
              console (vlm-xorg-keyboard-layout-base-type record) offset)
            (assert (eq name (sys:keyboard-layout-type keyboard)))
            (assert (mapped-p backspace #\Help))
            (assert (mapped-p delete #\Backspace))
            (vlm-disable-xorg-keyboard console)
            (assert (eq (sys:keyboard-layout-type keyboard)
                        (vlm-xorg-keyboard-layout-base-type record)))
            (assert (mapped-p backspace #\Backspace))
            (assert (mapped-p delete #\Rubout))
            ;; The patch must not change letters, function keys, or modifiers.
            (dotimes (shift (array-dimension table 0))
              (dotimes (code (array-dimension table 1))
                (unless (or (= code backspace) (= code delete))
                  (assert (eql (aref table shift code)
                               (aref initial-table shift code))))))
            (vlm-enable-xorg-keyboard console)
            (assert (eq name (sys:keyboard-layout-type keyboard)))
            (assert (mapped-p backspace #\Help)))
        (setq cli::*keyboard-table-remappings* saved-remappings)
        (copy-array-contents saved-defaults defaults)
        (vlm-enable-xorg-keyboard console))
      (assert (mapped-p backspace #\Rubout))
      (assert (mapped-p delete #\Backspace)))
    (format t "~&PASS: layout, swap, refresh, saved overrides, redetection, disable/enable.~%")
    t))

(test-vlm-xorg-keyboard)
