# dhtdigg

ddhtdigg uses the work of Juliusz Chroboczek (dht.c & dht.h) to access the
mainline dht network and capture announced peer addresses and thier 
associated info_hashes. Once captured dhtdigg attempts to create a BT
connection and request the metadata associated with that info_hash. If
successful the metadata is saved as a .torrent and then parsed into a 
Sqlite database named dhtdigg.db and then deleted.


NOTE:     dhtdigg does not download or share any of the bittorrent pieces
          containing the content being shared via bittorent. dhtdigg only
          downloads the metadata and then disconnects.

TODO:
 - currently seach does nothing. 
 - eliminte unnecessary file creation and parse in memory to database
 - Currently not capturing IPv6 peers.
