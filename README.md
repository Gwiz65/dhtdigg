# dhtdigg

dhtdigg uses the work of Juliusz Chroboczek (dht.c & dht.h) to access the
mainline dht network and capture announced peer addresses and thier 
associated info_hashes. Once captured dhtdigg attempts to create a BT
connection and request the metadata associated with that info_hash. If
successful the metadata is saved as a .torrent file in the 
".dhtdigg/torrents" directory created in the current user's home directory.

*WARNING* 
The saved .torrent files are UNFILTERED and UNCENSORED and possibly
point to copyrighted material. Open in your bittorrent client at 
your own risk. 

Dhtdigg does not download or share any of the bittorrent pieces
containing the content being shared via bittorent. Dhtdigg only 
downloads the metadata and then disconnects.

TODO:
 - next step is to curate the produced .torrent files into a searchable
   database that provides magnet links. 

 - Currently not capturing IPv6 peers.
         
         
