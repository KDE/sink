#!/bin/bash

echo "cm user.doe" | cyradm --auth PLAIN -u cyrus -w admin localhost

sudo echo "sam user.doe.* cyrus c;
dm user.doe.*;
sam user.doe cyrus c;
" | cyradm --auth PLAIN -u cyrus -w admin localhost
