;;; -*- Mode: LISP; Syntax: Common-Lisp; Package: USER; Base: 10 -*-
;;; Load in a fresh Genera test session. This enables ephemeral GC.
;;; Reproduce a transport trap between allocating and initializing a box.
;;; On an allocation-cache miss, the guest writes a fixnum into BAR1.
;;; Reading BAR1 must return a locative so saving it across GC relocates it
;;; along with the box. Otherwise initialization writes into the old copy,
;;; and BOX-EDGES reads DTP-NULL fields from the transported box.

(progn
  (si:gc-on :ephemeral t :dynamic nil)
  (assert
    (equal
      (multiple-value-list
        (funcall
          (compile nil
            '(lambda ()
               (let* ((area sys:working-storage-area)
                      (sample (dw::make-temporary-box 0 0 0 0))
                      (header (si:%memory-read sample :cycle-type si:%memory-header)))
                 (si:clear-cons-caches)
                 (let ((box (si:%make-structure si:dtp-instance si:dtp-header-p
                                                1 header area 5)))
                   (si::saving-registers-for-trap-for-effect
                     (si:ephemeral-gc-flip-area area :all-levels t)
                     (type-of box))
                   (si:%block-write 1 1)
                   (si:%block-write 1 2)
                   (si:%block-write 1 3)
                   (si:%block-write 1 4)
                   (dw::box-edges box)))))))
      '(1 2 3 4)))
  (format t "~&BAR GC regression passed.~%"))
