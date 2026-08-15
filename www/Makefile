all: subdirs thisdir

clean:

subdirs:
	@for i in a cmd f op plugin; do (cd $$i; make); done

thisdir: kwlist.js INTRO.html

.ALWAYS kwlist.js:
	sh scankeyword.sh

INTRO.html: INTRO.html.template
	sh scan.sh
