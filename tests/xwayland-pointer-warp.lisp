;;; -*- Mode: LISP; Syntax: Common-Lisp; Package: X-SCREEN; Base: 10 -*-
;;; Load after OG2-patches/xwayland-pointer-warp.lisp in a disposable Genera
;;; Xvfb console.  These checks exercise real CLX requests; they do not prove
;;; that a Wayland compositor honors the cursor-position hint.

(in-package "X-SCREEN")

(defmethod (test-vlm-xwayland-pointer-warp x-console) ()
  (let ((old-pointer have-pointer-p) (old-focus have-focus-p)
        (old-pending pending-warp-pointer)
        (old-enabled *vlm-xwayland-pointer-warp-enabled*)
        (cursor nil))
    (unwind-protect
        (progn
          (assert (gethash self *vlm-xwayland-cursors*))
          (let ((raster (make-array '(16 32) :element-type 'bit
                                   :initial-element 1)))
            (setq cursor (x-console-make-cursor self raster 16 16 0 0)))
          ;; Keep the mouse process from changing the cursor between a warp
          ;; and its assertions.  Release this lock before TRACK-MOUSE, which
          ;; also needs the screen controller lock.
          (xlib:with-display (display)
            (setq have-pointer-p t have-focus-p t pending-warp-pointer nil)
            (x-console-install-cursor self cursor)
            (x-console-warp-pointer self window 321 234)
            (multiple-value-bind (x y) (xlib:query-pointer window)
              (assert (and (= x 321) (= y 234))))
            (assert (eq (cdr (gethash self *vlm-xwayland-cursors*)) cursor))
            ;; Hidden cursors stay hidden.
            (x-console-install-cursor self nil)
            (x-console-warp-pointer self window 333 245)
            (multiple-value-bind (x y) (xlib:query-pointer window)
              (assert (and (= x 333) (= y 245))))
            (assert (eq (cdr (gethash self *vlm-xwayland-cursors*)) blank-cursor))
            ;; Both missing-focus cases retain the original deferred request.
            (x-console-install-cursor self cursor)
            (setq have-focus-p nil)
            (x-console-warp-pointer self window 345 256)
            (assert (equal pending-warp-pointer (list window 345 256)))
            (multiple-value-bind (x y) (xlib:query-pointer window)
              (assert (and (= x 333) (= y 245))))
            (setq have-focus-p t have-pointer-p nil)
            (x-console-warp-pointer self window 356 267)
            (assert (equal pending-warp-pointer (list window 356 267)))
            (setq have-pointer-p t)
            (apply #'x-console-warp-pointer self (shiftf pending-warp-pointer nil))
            (multiple-value-bind (x y) (xlib:query-pointer window)
              (assert (and (= x 356) (= y 267))))
            (vlm-disable-xwayland-pointer-warp)
            (assert (not *vlm-xwayland-pointer-warp-enabled*))
            (x-console-warp-pointer self window 367 278)
            (multiple-value-bind (x y) (xlib:query-pointer window)
              (assert (and (= x 367) (= y 278))))
            ;; Freeing an installed cursor keeps it visible in X, but its ID
            ;; must never be reused by the restoration code.
            (x-console-free-cursor self cursor)
            (setq cursor nil *vlm-xwayland-pointer-warp-enabled* t)
            (assert (null (gethash self *vlm-xwayland-cursors*)))
            (x-console-warp-pointer self window 370 280)
            (xlib:display-finish-output display)
            ;; A stale window record must not install its cursor on this window.
            (setf (gethash self *vlm-xwayland-cursors*) (cons nil :stale-cursor))
            (setq *vlm-xwayland-pointer-warp-enabled* t)
            (x-console-warp-pointer self window 378 289)
            (xlib:display-finish-output display))
          (vlm-enable-xwayland-pointer-warp self)
          (assert *vlm-xwayland-pointer-warp-enabled*)
          (assert (eq (car (gethash self *vlm-xwayland-cursors*)) window))
          (format t "~&PASS: warps, hidden/freed cursors, focus deferral/replay, stale window, disable/enable.~%"))
      (setq have-pointer-p old-pointer have-focus-p old-focus
            pending-warp-pointer old-pending
            *vlm-xwayland-pointer-warp-enabled* old-enabled)
      (vlm-refresh-xwayland-cursor self)
      (when cursor (x-console-free-cursor self cursor))))
  t)

(compile-flavor-methods x-console)
(test-vlm-xwayland-pointer-warp sys:*console*)
