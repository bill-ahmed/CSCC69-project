# CSCC69-Pintos
## Common Issues
1. I'm getting an error: `/bin/bash^M: bad interpreter`. What should I do? 

    a. This issue is caused by Linux and Windows having different ways of representing new lines. To fix, run: 
    
    `sed -i -e 's/\r$//' <path/to/file>`
