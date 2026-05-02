corpus: enwik8 enwik9 # calgary silesia
.PHONY: corpus

%: %.zip
	unzip $@
%.zip:
	wget https://www.mattmahoney.net/dc/$@
