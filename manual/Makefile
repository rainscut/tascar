all: manual.pdf callgraph.pdf 

diff: manual-diff_master.pdf

#TEXINPUTS = "./:../:./plugins/:../technical_report/:../flowcharts/fmt_eps/:../flowcharts/fmt_pdf/:../flowcharts/:"
TEXINPUTS="./:../doc/:"

%.pdf: %.tex
	TEXINPUTS=$(TEXINPUTS) pdflatex -draftmode -halt-on-error -file-line-error $<
	TEXINPUTS=$(TEXINPUTS) bibtex $(<:.tex=)
	TEXINPUTS=$(TEXINPUTS) pdflatex -halt-on-error -file-line-error $<
	TEXINPUTS=$(TEXINPUTS) makeindex $(basename $<)
	TEXINPUTS=$(TEXINPUTS) pdflatex -halt-on-error -file-line-error $<

%.aux: %.tex
	TEXINPUTS=$(TEXINPUTS) pdflatex $<

FIGURES = hoertechlogo.pdf filestructure.pdf creator.pdf
#ap_sndfile.pdf 

manual.pdf: $(FIGURES) secmodules.tex secactmodules.tex

secmodules.tex: $(wildcard mod*.tex)
	echo "List of general purpose modules:\\par\\\begin{itemize}" > $@
	echo $(patsubst %,\\item \\indmod{%},$(subst mod,,$(subst .tex,,$^))) >> $@
	echo "\\end{itemize}" >> $@
	echo "" >> $@
	echo $(patsubst %,\\input{%} ,$^) >> $@

secactmodules.tex: $(wildcard actmod*.tex)
	echo "List of actor modules:\\par\\\begin{itemize}" > $@
	echo $(patsubst %,\\item \\indmod{%},$(subst actmod,,$(subst .tex,,$^))) >> $@
	echo "\\end{itemize}" >> $@
	echo "" >> $@
	echo $(patsubst %,\\input{%} ,$^) >> $@

%.pdf: %.eps
	epstopdf $< -o $@

%.pdf: %.dot
	dot -Tpdf < $< > $@

clean:
	rm -Rf *.log *.lot *.lof *.aux *.dep *.dvi *.toc *~ *.blg *.bbl *.brf manual.pdf plug_*.tex *.idx *.ilg *.out *.ind $(FIGURES) secmodules.tex secactmodules.tex
	$(MAKE) -C ./examples clean

%.png: %.dot
	dot -Tpng < $< > $@

fdp_%.png: %.dot
	cat $< | sed 's/subgraph[^{]*//1' | fdp -Tpng > $@

fdp_%.pdf: %.dot
	cat $< | sed 's/subgraph[^{]*//1' | fdp -Tpdf > $@

manual-diff%.tex: manual.tex manual$(*).tex
	latexdiff -c ld.cfg --exclude-safecmd="sum" --exclude-textcmd="section" --exclude-textcmd="subsection" --exclude-textcmd="subsubsection"  manual$(*).tex $< > $@

%.pdf: %.svg
	inkscape --export-pdf="$@" $<

.PHONY : manual_master.tex

manual_master.tex:
	rm -f manual_master.tex
	mv manual.tex manual_temp.tex
	git checkout master manual.tex
	mv manual.tex $@
	mv manual_temp.tex manual.tex