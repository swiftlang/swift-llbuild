;; llbuild project settings.

(require 'cc-styles)

;; Style for llbuild C++ code.
(c-add-style "llbuild"
             '("gnu"
	       (fill-column . 80)
	       (c++-indent-level . 2)
	       (c-basic-offset . 2)
	       (indent-tabs-mode . nil)
	       (c-offsets-alist . ((arglist-intro . ++)
				   (innamespace . 0)
				   (member-init-intro . ++)))))

(provide 'llbuild-project-settings)
