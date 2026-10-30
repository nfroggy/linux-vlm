;;; -*- Mode: LISP; Syntax: Common-Lisp; Package: X-SCREEN; Base: 10 -*-
;;;
;;; Allow Genera's menu pointer warps on Xwayland.
;;;
;;; Load in the main X console; no emulator rebuild is needed.  Save a new
;;; world to retain the patch.  The C cold-load window is not affected.
;;;
;;; Xwayland 24.1 permits warp emulation while the cursor is hidden.  Use
;;; Genera's existing blank cursor for the request, then restore the cursor.
;;; This is the hide/warp/show workaround also used by SDL's X11 backend.
;;; See WarpMouseInternal in:
;;; https://github.com/libsdl-org/SDL/blob/SDL2/src/video/x11/SDL_x11mouse.c
;;; The Wayland compositor must support pointer constraints and relative
;;; pointers.  Loading this file explicitly opts in; Xorg and Xwayland share
;;; a vendor string, so it is not used to guess which server is running.
;;;
;;; (x-screen::vlm-disable-xwayland-pointer-warp) restores ordinary warping.
;;; (x-screen::vlm-enable-xwayland-pointer-warp) enables the workaround again.

(in-package "X-SCREEN")

(defvar *vlm-xwayland-pointer-warp-enabled* nil)

;; X11's window cursor attribute is write-only.  Remember (window . cursor)
;; per console, including while disabled.  Checking the window identity also
;; prevents reuse of an old connection's resources after a console restart.
(defvar *vlm-xwayland-cursors* (make-hash-table :test #'eq))

(defwhopper (x-console-install-cursor x-console) (cursor)
  (catch-network-errors ()
    (xlib:with-display (display)
      (multiple-value-prog1 (continue-whopper cursor)
        (setf (gethash self *vlm-xwayland-cursors*)
              (cons window (or cursor blank-cursor)))))))

(defwhopper (x-console-free-cursor x-console) (cursor)
  (catch-network-errors ()
    (xlib:with-display (display)
      ;; X keeps a freed cursor visible while installed, but its resource ID
      ;; can no longer be used to reinstall it after hiding.  Wait for the
      ;; next normal installation when a blinker flushes its cursor cache.
      (let ((record (gethash self *vlm-xwayland-cursors*)))
        (when (and record (eq (cdr record) cursor))
          (remhash self *vlm-xwayland-cursors*)))
      (continue-whopper cursor))))

(defwhopper (x-console-warp-pointer x-console) (dst x y)
  ;; Leave the existing focus checks and pending-warp replay in charge.
  (if (and *vlm-xwayland-pointer-warp-enabled* have-pointer-p have-focus-p)
      (catch-network-errors ()
        ;; Serialize the entire sequence with cursor installations and other
        ;; requests on this display.  CLX permits nested WITH-DISPLAY calls.
        (xlib:with-display (display)
          (let ((record (gethash self *vlm-xwayland-cursors*)))
            (if (and record (eq (car record) window)
                     (not (eq (cdr record) blank-cursor)))
                (unwind-protect
                    (progn
                      ;; Do not call INSTALL-CURSOR here: the remembered
                      ;; cursor must remain the one we intend to restore.
                      (setf (xlib:window-cursor window) blank-cursor)
                      (continue-whopper dst x y))
                  (setf (xlib:window-cursor window) (cdr record))
                  (xlib:display-force-output display))
              ;; A hidden cursor already permits warps.  If we have not yet
              ;; observed an installation, leave its unknown shape alone.
              (continue-whopper dst x y)))))
    (continue-whopper dst x y)))

(defmethod (sys:console-disable x-console :after) (&key abort)
  (declare (ignore abort))
  (remhash self *vlm-xwayland-cursors*))

(defmethod (vlm-refresh-xwayland-cursor x-console) ()
  ;; Seed tracking when loading into an already running console.  TRACK-MOUSE
  ;; reinstalls the current blinker through the normal cursor installation
  ;; path, including blinkers drawn in software rather than by the X server.
  (when (and cli::mouse (tv:mouse-blinker cli::mouse))
    (send (tv:mouse-blinker cli::mouse) :track-mouse)))

(defun vlm-enable-xwayland-pointer-warp (&optional (console sys:*console*))
  (vlm-refresh-xwayland-cursor console)
  (setq *vlm-xwayland-pointer-warp-enabled* t))

(defun vlm-disable-xwayland-pointer-warp ()
  (setq *vlm-xwayland-pointer-warp-enabled* nil))

(compile-flavor-methods x-console)

(vlm-enable-xwayland-pointer-warp)
