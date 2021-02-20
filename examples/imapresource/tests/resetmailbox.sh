#!/bin/bash

echo "cm user.doe" | cyradm --auth PLAIN -u cyrus -w admin localhost

sudo echo "sam user.doe.* cyrus c;
dm user.doe.*;
cm user.doe.test;
cm user.doe.Drafts;
cm user.doe.Trash;
sam user.doe cyrus c;
" | cyradm --auth PLAIN -u cyrus -w admin localhost

sudo echo "sam user.doe.* cyrus c;
subscribe INBOX.test;
subscribe INBOX.Drafts;
subscribe INBOX.Trash;
" | cyradm --auth PLAIN -u doe -w doe localhost
