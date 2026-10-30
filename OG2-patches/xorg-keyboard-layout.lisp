;;; -*- Mode: LISP; Syntax: Common-Lisp; Package: X-SCREEN; Base: 10 -*-
;;;
;;; Keyboard Control for a US PC keyboard on Xorg/Xwayland.
;;;
;;; Load this file in the main Genera X console, then use:
;;;   Select Activity Keyboard Control
;;; Backspace produces Rubout; Delete produces Genera Backspace.
;;; Save a new world to keep the patch across boots.  No emulator rebuild is
;;; needed.  The separate C cold-load window is not affected.
;;;
;;; The existing X keyboard translations are retained.  The diagram uses a
;;; US ANSI PC template, resolving its keys through the current X keymap rather
;;; than assuming Linux, NDS, or DEC keycodes.  Missing keys are omitted; media
;;; keys and non-US geometry are not drawn.  Alt/Meta aliases accommodate the
;;; VLM's modifier setup.  Keypad navigation symbols accommodate Num Lock.
;;;
;;; Keyboard Control's Save Differences puts Lisp forms on the kill ring.
;;; Evaluate those forms (or put them in your init file) to retain edits when
;;; the X keyboard table is rebuilt.  Saved overrides take precedence here.
;;;
;;; To disable this patch in a console:
;;;   (x-screen::vlm-disable-xorg-keyboard)
;;; To enable it again:
;;;   (x-screen::vlm-enable-xorg-keyboard)

(in-package "X-SCREEN")

(defvar *vlm-xorg-keyboard-enabled* t)

;; Keep these records in a saved world.  Different keycodes or underlying
;; translation profiles get different layout names, so their saved overrides
;; cannot accidentally be applied to one another.
(defvar *vlm-xorg-keyboard-layouts* nil)

(defstruct (vlm-xorg-keyboard-layout (:predicate nil))
  name base-type keys)

;; Each row is (left top (legend keysym-or-alternatives [width [height]]) ...).
;; Numeric X keysyms avoid depending on newer CLX keysym-name definitions.
(defparameter *vlm-xorg-pc-rows*
  '((0 0 ("Esc" #xff1b))
    (2 0 ("F1" #xffbe) ("F2" #xffbf) ("F3" #xffc0) ("F4" #xffc1))
    (6.5 0 ("F5" #xffc2) ("F6" #xffc3) ("F7" #xffc4) ("F8" #xffc5))
    (11 0 ("F9" #xffc6) ("F10" #xffc7) ("F11" #xffc8) ("F12" #xffc9))
    (16 0 ("Print" #xff61) ("Scroll" #xff14) ("Pause" (#xff13 #xff6b)))
    (0 2 ("`" #x60) ("1" #x31) ("2" #x32) ("3" #x33) ("4" #x34)
         ("5" #x35) ("6" #x36) ("7" #x37) ("8" #x38) ("9" #x39)
         ("0" #x30) ("-" #x2d) ("=" #x3d) ("Backspace" #xff08 2))
    (0 3 ("Tab" #xff09 1.5) ("Q" #x71) ("W" #x77) ("E" #x65)
         ("R" #x72) ("T" #x74) ("Y" #x79) ("U" #x75) ("I" #x69)
         ("O" #x6f) ("P" #x70) ("[" #x5b) ("]" #x5d) ("\\" #x5c 1.5))
    (0 4 ("Caps Lock" #xffe5 1.75) ("A" #x61) ("S" #x73) ("D" #x64)
         ("F" #x66) ("G" #x67) ("H" #x68) ("J" #x6a) ("K" #x6b)
         ("L" #x6c) (";" #x3b) ("'" #x27) ("Enter" #xff0d 2.25))
    (0 5 ("Left Shift" #xffe1 2.25) ("Z" #x7a) ("X" #x78) ("C" #x63)
         ("V" #x76) ("B" #x62) ("N" #x6e) ("M" #x6d) ("," #x2c)
         ("." #x2e) ("/" #x2f) ("Right Shift" #xffe2 2.75))
    (0 6 ("Left Ctrl" #xffe3 1.25) ("Left Super" #xffeb 1.25)
         ("Left Alt" (#xffe9 #xffe7) 1.25) ("Space" #x20 6.25)
         ("Right Alt" (#xffea #xffe8 #xfe03) 1.25)
         ("Right Super" #xffec 1.25) ("Menu" (#xff67 #xffee) 1.25)
         ("Right Ctrl" #xffe4 1.25))
    (16 2 ("Insert" #xff63) ("Home" #xff50) ("Page Up" #xff55))
    (16 3 ("Delete" #xffff) ("End" #xff57) ("Page Down" #xff56))
    (17 5 ("Up" #xff52))
    (16 6 ("Left" #xff51) ("Down" #xff54) ("Right" #xff53))
    (20 2 ("Num Lock" #xff7f) ("KP /" #xffaf) ("KP *" #xffaa)
          ("KP -" #xffad))
    (20 3 ("KP 7" (#xff95 #xffb7)) ("KP 8" (#xff97 #xffb8))
          ("KP 9" (#xff9a #xffb9)) ("KP +" #xffab 1 2))
    (20 4 ("KP 4" (#xff96 #xffb4)) ("KP 5" (#xff9d #xffb5))
          ("KP 6" (#xff98 #xffb6)))
    (20 5 ("KP 1" (#xff9c #xffb1)) ("KP 2" (#xff99 #xffb2))
          ("KP 3" (#xff9b #xffb3)) ("KP Enter" #xff8d 1 2))
    (20 6 ("KP 0" (#xff9e #xffb0) 2) ("KP ." (#xff9f #xffae)))))

(defun vlm-xorg-display-p (display)
  (let ((vendor (xlib:display-vendor-name display)))
    (or (search "X.Org" vendor) (search "XFree86" vendor)
        (search "Xwayland" vendor))))

(defun vlm-xorg-keycode (display keysyms)
  ;; Search only the unshifted keysym.  A symbol on an AltGr/Shift level does
  ;; not establish the position of its physical key in the US template.
  (loop for keysym in (if (listp keysyms) keysyms (list keysyms))
        thereis (loop for code from (xlib:display-min-keycode display)
                            to (xlib:display-max-keycode display)
                      when (= (xlib:keycode->keysym display code 0) keysym)
                        return code)))

(defun vlm-xorg-layout-keys (display offset limit)
  (let ((keys nil) (used nil))
    (dolist (row *vlm-xorg-pc-rows*)
      (destructuring-bind (left top &rest entries) row
        (dolist (entry entries)
          (destructuring-bind (legend keysyms &optional (width 1) (height 1)) entry
            (let* ((code (vlm-xorg-keycode display keysyms))
                   (index (and code (- code offset))))
              (when (and index (<= 0 index) (< index limit)
                         (not (member index used)))
                (push index used)
                (push (list :name legend :legend legend :code index
                            :edges (list left top (+ left width) (+ top height)))
                      keys)))
            (incf left width)))))
    (nreverse keys)))

(defun vlm-xorg-layout-record (name)
  (find name *vlm-xorg-keyboard-layouts* :key #'vlm-xorg-keyboard-layout-name))

(defun vlm-xorg-apply-saved-mappings (name table)
  (dolist (remapping cli::*keyboard-table-remappings*)
    (destructuring-bind (value layout-type &rest options) remapping
      (when (eq layout-type name)
        (apply #'sys:set-keyboard-table-mapping value table options)))))

(defmethod (vlm-install-xorg-keyboard x-console) ()
  (when (and *vlm-xorg-keyboard-enabled* (vlm-xorg-display-p display))
    (let* ((table (sys:keyboard-keyboard-table cli::keyboard))
           (limit (sys:keyboard-table-key-code-limit table))
           (old-type (sys:keyboard-layout-type cli::keyboard))
           (old-record (vlm-xorg-layout-record old-type))
           (base-type (if old-record
                          (vlm-xorg-keyboard-layout-base-type old-record)
                          old-type))
           (backspace (vlm-xorg-keycode display #xff08))
           (delete (vlm-xorg-keycode display #xffff))
           (keys (vlm-xorg-layout-keys display keycode-offset limit)))
      ;; Decline an incompatible display instead of installing a partial swap.
      (when (and backspace delete
                 (<= 0 (- backspace keycode-offset))
                 (< (- backspace keycode-offset) limit)
                 (<= 0 (- delete keycode-offset))
                 (< (- delete keycode-offset) limit))
        (sys:set-keyboard-table-mapping #\Rubout table
                                       (- backspace keycode-offset) :all-shifts t)
        (sys:set-keyboard-table-mapping #\Backspace table
                                       (- delete keycode-offset) :all-shifts t)
        (let* ((record (find-if
                        #'(lambda (record)
                            (and (eq base-type (vlm-xorg-keyboard-layout-base-type record))
                                 (equal keys (vlm-xorg-keyboard-layout-keys record))))
                        *vlm-xorg-keyboard-layouts*))
               (name (if record (vlm-xorg-keyboard-layout-name record)
                         (intern (format nil "VLM-XORG-PC-~D"
                                         (1+ (length *vlm-xorg-keyboard-layouts*)))
                                 "KEYWORD"))))
          (unless record
            (let ((mapping (or (layout-type-keyboard-mapping base-type)
                               (layout-type-keyboard-mapping :default))))
              (define-keyboard-mapping-internal
                name (list :leds (copy-tree (keyboard-mapping-leds mapping)))
                (copy-tree (keyboard-mapping-keys mapping))))
            (system-applications::define-keyboard-layout-2
              name keys :pretty-name (format nil "Xorg US PC (~A)" name)
              :default-mapping-table (sys:copy-keyboard-table table))
            (push (make-vlm-xorg-keyboard-layout
                    :name name :base-type base-type :keys keys)
                  *vlm-xorg-keyboard-layouts*)
            ;; Register once, not on every refresh: repeated definitions while
            ;; loading another file provoke Genera's source-ownership query.
            ;; The update hook also accommodates detect-keyboard-patch.lisp,
            ;; which may select its own :XORG-PC signature on a later boot.
            (define-keyboard-signature-internal
              name (list :vendor-name (xlib:display-vendor-name display)
                         :keycode-offset keycode-offset)
              (display-keyboard-signature-keys display)))
          (setf (sys:keyboard-layout-type cli::keyboard) name)
          ;; The primary update method applied overrides before our default
          ;; swap.  Apply them once more so explicit user choices win.
          (vlm-xorg-apply-saved-mappings name table)
          name)))))

;; This path runs at console initialization, reconnect, Set X Keyboard
;; Mapping, and X MappingNotify.  It does not replace the existing translator.
(defmethod (x-console-update-keyboard-mapping x-console :after) ()
  (vlm-install-xorg-keyboard self))

(defun vlm-enable-xorg-keyboard (&optional (console sys:*console*))
  (setq *vlm-xorg-keyboard-enabled* t)
  (x-console-update-keyboard-mapping console)
  (sys:keyboard-layout-type (sys:console-keyboard console)))

(defun vlm-disable-xorg-keyboard (&optional (console sys:*console*))
  (setq *vlm-xorg-keyboard-enabled* nil)
  (let* ((keyboard (sys:console-keyboard console))
         (record (vlm-xorg-layout-record (sys:keyboard-layout-type keyboard))))
    (when record
      (console-set-keyboard-type console (vlm-xorg-keyboard-layout-base-type record)
                                (x-console-keycode-offset console))))
  nil)

(compile-flavor-methods x-console)

(vlm-enable-xorg-keyboard)
