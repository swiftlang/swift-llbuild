;;; Directory Local Variables
;;; For more information see (info "(emacs) Directory Variables")


((nil
  (eval .
        ;; Auto-load the llbuild project settings.
        (unless (featurep 'llbuild-project-settings)
          (message "loading 'llbuild-project-settings")
          ;; Make sure the project's own utils directory is in the load path,
          ;; but don't override any one the user might have set up.
          (add-to-list
           'load-path
           (concat
            (let ((dlff (dir-locals-find-file default-directory)))
              (if (listp dlff) (car dlff) (file-name-directory dlff)))
            "utils/emacs")
           :append)
          (require 'llbuild-project-settings))))
 
 (c++-mode
  ;; Load the llbuild style, if available.
  ;;
  ;; We have to do deferred loading of the style like this (instead of just
  ;; setting the c-file-style variable, so that this works even when we are
  ;; auto-loading llbuild-project-settings. Otherwise, that would fail since
  ;; dir-locals evaluates the variables before it evaluates the eval list.
  (eval .
        (if (assoc "llbuild" c-style-alist)
            (c-set-style "llbuild"))))

 (c-mode
  ;; Load the llbuild style, if available.
  ;;
  ;; We have to do deferred loading of the style like this (instead of just
  ;; setting the c-file-style variable, so that this works even when we are
  ;; auto-loading llbuild-project-settings. Otherwise, that would fail since
  ;; dir-locals evaluates the variables before it evaluates the eval list.
  (eval .
        (if (assoc "llbuild" c-style-alist)
            (c-set-style "llbuild")))))
