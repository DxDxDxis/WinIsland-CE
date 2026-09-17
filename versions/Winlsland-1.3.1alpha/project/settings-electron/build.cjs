const fs=require('node:fs');for(const file of ['index.html','style.css'])fs.copyFileSync('src/'+file,'dist/'+file);

fs.rmSync('dist/preview-worker.js', {force:true});
