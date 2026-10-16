/* Installed as libwebkitty.a: the WebKit libraries and their dependencies,
   grouped since they reference each other. ICU reads its data as files from
   PROGDIR:Resources (Developer/share/icu/data), so only the stub is linked. */
GROUP ( -lwebkitty-webkitlegacy -lwebkitty-webcore -lwebkitty-pal
        -lwebkitty-javascriptcore -lwebkitty-wtf
        -lcairo -lpixman -lfontconfig -lfreetype2 -lpng -lz -ljpeg
        -lwebpdemux -lwebp -lsharpyuv -lwoff2dec -lwoff2common -lbrotlidec -lbrotlicommon -lxslt -lxml2 -lsqlite3 -lpsl -lharfbuzz-icu -lharfbuzz
        -licui18n -licuuc -licudata_stub -lcurl -lssl -lcrypto -lzstd
        -lavformat -lavcodec -lswresample -lswscale -lavutil -lbz2 -llzma -liconv -latomic
        -lopenurl -lpthread )
