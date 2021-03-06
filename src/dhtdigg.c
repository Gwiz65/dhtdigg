/****************************************************************************
 * dhtdigg.c                                                                *
 * Copyright (C) 2018 Gwiz <gwiz2009@gmail.com>                             *
 *                                                                          *
 * dhtdigg is free software: you can redistribute it and/or modify it       *
 * under the terms of the GNU General Public License as published by the    *
 * Free Software Foundation, either version 3 of the License, or            *
 * (at your option) any later version.                                      *
 *                                                                          *
 * dhtdigg is distributed in the hope that it will be useful, but           *
 * WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                     *
 * See the GNU General Public License for more details.                     *
 *                                                                          *
 * You should have received a copy of the GNU General Public License along  *
 * with this program.  If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                          *
 ****************************************************************************/
#include "dhtdigg.h" 

/****************************
 *       Defines            *
 ****************************/
// use the local (not installed) ui file
// #define UI_FILE "src/dhtdigg.ui"

// use the installed ui file
#define UI_FILE PACKAGE_DATA_DIR"/ui/dhtdigg.ui"

#define MAX_PEERPROSPECTS 64
#define MSGBUFFERSIZE 65536
#define METADATBUFFERSIZE 6291456

/****************************
 *       Globals            *
 ****************************/
int dhtpipe[2];
int btpipe[2];
FILE *bt_display;
GtkTextBuffer *DHTtextbuffer;
GtkTextBuffer *BTtextbuffer;
GtkScrolledWindow *DHTWindowScrollView;
GtkScrolledWindow *BTWindowScrollView;
GtkLabel *TorrentNameLabel;
GtkLabel *HashLabel;
GtkLabel *LastSeenLabel;
GtkLabel *LengthLabel;
GtkLabel *PrivateLabel;
GtkLabel *RecordCount;
GtkListStore *FileList;
GtkTreeIter iter;
GtkLinkButton *MagnetLink;
gboolean dhtloop = TRUE;
gboolean getmetadataloop = TRUE;
static unsigned char buf[4096];
int s = -1;
int s6 = -1;
struct peerlist PeerListQueue[MAX_PEERPROSPECTS];
int peerlistopenslot = 0;
int peerlistnum = 0;
struct bootstrap_storage Bootstrap;
gchar *workdir;
sqlite3 *torrent_db;
int totalrecords = 0;
int currentrowid = 0;
int recordsbeyond = 0;
int rowid_ret = 0;
char metadatabuffer[METADATBUFFERSIZE]; // 6 MB buffer

/****************************************************************************
 *                                                                          *
 * Function: getrowid_ret                                                *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int getrowid_ret(void *data, int argc, char **argv, char **azColName)
{
	rowid_ret = (strtol(argv[0], NULL, 10));
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: getrecordsbeyond                                                *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int getrecordsbeyond(void *data, int argc, char **argv, char **azColName)
{
	recordsbeyond = (strtol(argv[0], NULL, 10));
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: gettotalrecords                                                *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int gettotalrecords(void *data, int argc, char **argv, char **azColName)
{
	totalrecords = (strtol(argv[0], NULL, 10));
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: getfiles                                                       *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int getfiles(void *data, int argc, char **argv, char **azColName)
{
	//append line to list store
	gtk_list_store_append (FileList, &iter);
	gtk_list_store_set (FileList, &iter, 0, 
	                    g_format_size(strtol(argv[1], NULL, 10)), -1);
	gtk_list_store_set (FileList, &iter, 1, argv[2], -1);
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: getrecord                                                      *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int getrecord(void *data, int argc, char **argv, char **azColName)
{
	char temp1[32];
	char temp2[32];
	int dstptr = 0;
	int srcptr = 0;
	char *err_msg = 0;
	gchar *sqlcmd = "";
	char magtext[4096];

	// set currentrowid
	currentrowid = strtol(argv[0], NULL, 10);
	// set display
	gtk_label_set_text (TorrentNameLabel, argv[2]); 
	gtk_label_set_text (HashLabel, argv[1]);
	// magnet:?xt=urn:btih:edba09681fa806510a6a434e6cdeb4ab9f2d14d7&dn=the.crown.S02E10.HDTV.SubtituladoEsp.SC.avi
	sprintf(magtext, "magnet:?xt=urn:btih:%s&dn=%s", argv[1], argv[2]);
	gtk_button_set_label ((GtkButton *) MagnetLink, magtext);
	gtk_link_button_set_uri (MagnetLink, magtext);
	// format last seen time
	sprintf(temp1, "%s", argv[3]);
	while (srcptr < strlen(temp1))
	{
		if ((srcptr == 4) || (srcptr == 6))
		{
			temp2[dstptr] = '-';
			dstptr++;
		}
		if (srcptr == 8)
		{
			temp2[dstptr] = ' ';
			dstptr++;
			temp2[dstptr] = ' ';
			dstptr++;
		}
		if ((srcptr == 10) || (srcptr == 12))
		{
			temp2[dstptr] = ':';
			dstptr++;
		}
		temp2[dstptr] = temp1[srcptr];
		srcptr++;
		dstptr++;
	}
	temp2[dstptr] = 0;
	gtk_label_set_text (LastSeenLabel, temp2);
	gtk_label_set_text (LengthLabel, g_format_size (strtol(argv[4], NULL, 10)));
	if (strtol(argv[5], NULL, 10) == 1)
		gtk_label_set_text (PrivateLabel, "Yes");
	else
		gtk_label_set_text (PrivateLabel, "No");
	// clear list store
	gtk_list_store_clear (FileList);
	// query file records
	sqlcmd = g_strconcat("SELECT * FROM files WHERE hash = '", argv[1],
	                     "';", NULL);
	if (!((sqlite3_exec(torrent_db, sqlcmd, getfiles, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: display_record                                                 *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void display_record(void)
{
	char *err_msg = 0;

	// get number of rows
	totalrecords = 0;
	if (!((sqlite3_exec(torrent_db, 
	                    "SELECT count(rowid) FROM hash;",
	                    gettotalrecords, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	if (totalrecords == 0)
	{
		gtk_label_set_text (TorrentNameLabel, ""); 
		gtk_label_set_text (HashLabel, "");
		gtk_button_set_label ((GtkButton *) MagnetLink, "");
		gtk_link_button_set_uri (MagnetLink, "");
		gtk_label_set_text (LastSeenLabel, "");
		gtk_label_set_text (LengthLabel, "");
		gtk_label_set_text (PrivateLabel, "");
		gtk_label_set_text (RecordCount, "0 of 0");
		// clear list store
		gtk_list_store_clear (FileList);
		currentrowid = 0;
	}
	else
	{
		if (currentrowid == 0)
		{
			// show first record
			if (!((sqlite3_exec(torrent_db, 
			                    "SELECT rowid, * FROM hash ORDER BY ROWID ASC LIMIT 1;",
			                    getrecord, 0, &err_msg)) == SQLITE_OK))
			{
				printf("SQL error: %s\n", err_msg);
				sqlite3_free(err_msg);
			}
		}
		else
		{
			char sqlcmd[1024];

			// show currentrowid record
			sprintf(sqlcmd, "SELECT rowid, * FROM hash WHERE rowid=%i;",
			        currentrowid);
			if (!((sqlite3_exec(torrent_db, sqlcmd, getrecord, 0, &err_msg)) == SQLITE_OK))
			{
				printf("SQL error: %s\n", err_msg);
				sqlite3_free(err_msg);
			}
		}
		// determine place and display
		{
			char sqlcmd[1024];
			char temp[100];
			int recordnum = 0;

			recordsbeyond = 0;
			sprintf(sqlcmd, "SELECT count(rowid) FROM hash WHERE rowid > %i;",
			        currentrowid);
			if (!((sqlite3_exec(torrent_db, sqlcmd, getrecordsbeyond, 0, &err_msg)) == SQLITE_OK))
			{
				printf("SQL error: %s\n", err_msg);
				sqlite3_free(err_msg);
			}
			recordnum = totalrecords - recordsbeyond;
			if (recordnum < 1) recordnum = 1;
			sprintf(temp, "%i of %i", recordnum, totalrecords);
			gtk_label_set_text (RecordCount, temp);
		}
	}
}

/****************************************************************************
 *                                                                          *
 * Function: first_button_clicked                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void first_button_clicked (GtkButton *button, gpointer user_data)
{
	char *err_msg = 0;

	// set current rowid to first
	if (!((sqlite3_exec(torrent_db, 
	                    "SELECT rowid FROM hash ORDER BY rowid ASC LIMIT 1;",
	                    getrowid_ret, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	currentrowid = rowid_ret;
	display_record();
}

/****************************************************************************
 *                                                                          *
 * Function: last_button_clicked                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void last_button_clicked (GtkButton *button, gpointer user_data)
{
	char *err_msg = 0;

	// set current rowid to last
	if (!((sqlite3_exec(torrent_db, 
	                    "SELECT rowid FROM hash ORDER BY rowid DESC LIMIT 1;",
	                    getrowid_ret, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	currentrowid = rowid_ret;
	display_record();
}

/****************************************************************************
 *                                                                          *
 * Function: back_button_clicked                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void back_button_clicked (GtkButton *button, gpointer user_data)
{
	char *err_msg = 0;
	char sqlcmd[1024];

	sprintf(sqlcmd, "SELECT rowid FROM hash WHERE rowid < %i ORDER BY rowid DESC LIMIT 1;",
	        currentrowid);
	if (!((sqlite3_exec(torrent_db, sqlcmd, getrowid_ret, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	currentrowid = rowid_ret;
	display_record();
}

/****************************************************************************
 *                                                                          *
 * Function: foward_button_clicked                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void foward_button_clicked (GtkButton *button, gpointer user_data)
{
	char *err_msg = 0;
	char sqlcmd[1024];

	sprintf(sqlcmd, "SELECT rowid FROM hash WHERE rowid > %i ORDER BY rowid ASC LIMIT 1;",
	        currentrowid);
	if (!((sqlite3_exec(torrent_db, sqlcmd, getrowid_ret, 0, &err_msg)) == SQLITE_OK))
	{
		printf("SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	currentrowid = rowid_ret;
	display_record();
}

/******************************************************************************
 *                                                                            *
 * Function: WriteBootstrapFile                                               *
 *                                                                            *
 * Purpose : collect some nodes and write them to bootstrap file              *
 *                                                                            *
 ******************************************************************************/
gboolean WriteBootstrapFile(void)
{
	struct sockaddr_in sin[6];
	struct sockaddr_in6 sin6[6];
	int num = 6, num6 = 6;
	char bsfile[PATH_MAX];
	int  bsfile_fd;
	int numnodes;

	// collect some known nodes
	numnodes = dht_get_nodes(sin, &num, sin6, &num6);
	if (num > 0)
	{
		int ctr = 0;
		while (ctr < num)
		{
			memcpy(&Bootstrap.IPv4bootnodes[ctr], &sin[ctr], sizeof(struct sockaddr_in));
			ctr++;
		}
		Bootstrap.numofIPv4s = ctr;
	}
	if (num6 > 0)
	{
		int ctr = 0;
		while (ctr < num6)
		{
			memcpy(&Bootstrap.IPv6bootnodes[ctr], &sin6[ctr], sizeof(struct sockaddr_in6));
			ctr++;
		}
		Bootstrap.numofIPv6s = ctr;
	}
	// set filename
	sprintf(bsfile, "%sdhtdigg.bs", workdir);
	// create & write bootstrap file
	bsfile_fd = open(bsfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG
	                 | S_IRWXO);
	write(bsfile_fd, &Bootstrap, sizeof(struct bootstrap_storage));
	fsync(bsfile_fd);
	close(bsfile_fd);
	fprintf(dht_debug, "Saving %i nodes to bootstrap file.\n", numnodes);
	fflush(dht_debug);
	return FALSE; // run once
}

/******************************************************************************
 *                                                                            *
 * Function: SendBTmsg                                                        *
 *                                                                            *
 * Purpose : returns bytes sent minus size header, 0=disconnect, -1=error     *
 *           payload points to message without the 4 byte message size header *
 *           payload_size is number of bytes of payload                       * 
 *                                                                            *
 ******************************************************************************/
int SendBTmsg(int sockfd, char * payload, uint32_t payload_size)
{

	uint32_t bemsgsize = 0;
	int ret = 0;

	// send message size
	bemsgsize = htonl(payload_size);
	ret = send(sockfd, &bemsgsize, 4, 0);
	if (ret == 0) return 0;
	if (ret < 0) return -1;
	if (ret != 4) return -1;
	// send message
	ret = send(sockfd, payload, payload_size, 0);
	if (ret == 0) return 0;
	if (ret < 0) return -1;
	if (ret != payload_size) return -1;
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: GetBTmsg                                                         *
 *                                                                            *
 * Purpose : returns size of payload, 0=disconnect, -1=errror, -2=strikeout   *
 *           fills payload with recieved message                              *
 *           btmsgwant indicates the bittorent message we are looking for     *
 *           exmsgwant indicates the extended message we are looking for      *
 *                                                                            *
 *           This function will read 3 messages looking for what we want      *
 *           and will respond with keepalive to nonappicable messages         *				
 *                                                                            *
 ******************************************************************************/
int GetBTmsg(int sockfd, char * payload, int btmsgwant, int exmsgwant)
{
	int bGotWanted = FALSE;
	int tries = 0;
	int bytesread = 0;

	while (bGotWanted == FALSE)
	{
		char msg_len[4];
		int msgsize= 0;
		uint32_t tmp;

		if (tries > 2) return -2;
		// read length
		bytesread = 0;
		while (bytesread < 4)
		{
			int recvd = 0;
			recvd = recv(sockfd, &msg_len[bytesread], 4 - bytesread, 0);
			if (recvd == 0) return 0;
			if (recvd == -1) return -1;
			bytesread = bytesread + recvd;
		}
		if (bytesread != 4) return -1;
		memcpy(&tmp, &msg_len, 4);
		msgsize = ntohl(tmp);
		// read rest of msg
		bytesread = 0;
		while (bytesread < msgsize)
		{
			int recvd = 0;
			recvd = recv(sockfd, &payload[bytesread], msgsize - bytesread, 0);
			if (recvd == 0) return 0;
			if (recvd == -1) return -1;
			bytesread = bytesread + recvd;
		}
		if (bytesread != msgsize) return -1;
		if (payload[0] == btmsgwant && payload[1] == exmsgwant)
			bGotWanted = TRUE;
		else
		{
			if (payload[0] == 5 || payload[0] == 4)
			{
				// bitfield or have messages are balls, not strikes
				tries--;
			}
			else
			{
				// send keepalive
				fprintf(bt_display, "Got BT message %i. Sending keepalive.\n", payload[0]);
				fflush(bt_display);
				uint32_t keepalive = 0;
				send(sockfd, &keepalive, 4, 0);
			}
		}
		tries++;
	}
	return bytesread;
}

/******************************************************************************
 *                                                                            *
 * Function: TimedConnect                                                     *
 *                                                                            *
 * Purpose : socket connect with timeout                                      *
 *                                                                            *
 ******************************************************************************/
int TimedConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen, int secs)
{
	fd_set fdset;
	struct timeval tv;

	fcntl(sockfd, F_SETFL, O_NONBLOCK);
	connect(sockfd, addr, addrlen);
	FD_ZERO(&fdset);
	FD_SET(sockfd, &fdset);
	tv.tv_sec = secs;             
	tv.tv_usec = 0;
	if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1)
	{
		int so_error;
		socklen_t len = sizeof(so_error);

		getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
		if (so_error == 0) 
		{
			fcntl(sockfd, F_SETFL, O_ASYNC);
			return 0;
		}
		else return -1;
	}
	else return -1;
}

/******************************************************************************
 *                                                                            *
 * Function: RestartDHT                                                       *
 *                                                                            *
 * Purpose :                                                                  *
 *                                                                            *
 ******************************************************************************/
gboolean RestartDHT (void)
{
	WriteBootstrapFile();
	// stop thread
	dhtloop = FALSE;
	//pause to let dht close down
	sleep(30);
	// send reset messages
	fprintf(dht_debug, " \n \n \n \n******************************\n");
	fflush(dht_debug);
	fprintf(dht_debug, "Restarting DHT......\n");
	fflush(dht_debug);
	fprintf(dht_debug, "******************************\n");
	fflush(dht_debug);
	fflush(dht_debug);
	// reset stuff
	dhtloop = TRUE;
	s = -1;
	s6 = -1;
	// start dht thread
	g_thread_new ("dhtthread", (GThreadFunc) DhtThread, NULL);
	return TRUE; // keep running
}

/****************************************************************************
 *                                                                          *
 * Function: GetMetadataThread                                              *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
gint GetMetadataThread (void) 
{
	while (getmetadataloop)
	{
		// see if we have something to connect to 
		if (!memcmp(&PeerListQueue[peerlistnum].info_hash, &zeros, 20) == 0)
		{
			int peer_sock_fd;
			struct sockaddr_in peer_sock_addr;

			fprintf(bt_display, "Attempting peer connect from slot %i.\n", peerlistnum);
			fflush(bt_display);
			// fill sockaddr
			peer_sock_addr.sin_family = AF_INET;
			memcpy(&(peer_sock_addr.sin_addr), &(PeerListQueue[peerlistnum].addr), 4);
			memcpy(&(peer_sock_addr.sin_port), &(PeerListQueue[peerlistnum].prt), 2);
			// create socket
			peer_sock_fd = socket(AF_INET, SOCK_STREAM, 0); 
			// connect with timeout
			if (!(TimedConnect(peer_sock_fd, (const struct sockaddr*) &peer_sock_addr, 
			                   sizeof(struct sockaddr_in), 12) < 0 ))
			{
				unsigned char handshake_msg[68];
				int hshake_sent;
				struct timeval timeout;      

				// set read & write timeouts
				timeout.tv_sec = 30;
				timeout.tv_usec = 0;
				setsockopt (peer_sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
				setsockopt (peer_sock_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
				// send bt handshake
				memset(&handshake_msg[0], 0x0, 68);
				memset(&handshake_msg[0], 0x13, 1);
				strcpy((char *) &handshake_msg[1], "BitTorrent protocol");
				//(reserved_byte[5] & 0x10) Support Extended
				memset(&handshake_msg[25], 0x10, 1);
				memcpy(&handshake_msg[28], &PeerListQueue[peerlistnum].info_hash, 20);
				// give us a cool peer id
				strcpy((char *) &handshake_msg[48], "-DhtDigg");
				// random for rest of id
				dht_random_bytes(&handshake_msg[56], 12);
				// send handshake
				hshake_sent = send(peer_sock_fd, &handshake_msg, 68, 0);
				if (hshake_sent == 68)
				{
					unsigned char handshakebuffer[69]; 
					int hshake_recd;
					char temp[2048];

					memset(&handshakebuffer[0], 0x0, 68);
					fprintf(bt_display, "Handshake message sent.\n");
					fflush(bt_display);
					// receive return handshake
					hshake_recd = recv(peer_sock_fd, handshakebuffer, 68, 0);
					if (hshake_recd == 68)
					{
						strncpy(&temp[0], (char *) &handshakebuffer[48], 8);
						temp[8] = '\0';
						fprintf(bt_display, "Received handshake. Client code: %s\n", temp);
						fflush(bt_display);
						// check for extension bit
						if (handshakebuffer[25] & 0x10)
						{
							//char extensionhandshake[2048];
							unsigned char msgbuffer[MSGBUFFERSIZE];
							int msgsize = 0;
							int sent = 0;

							fprintf(bt_display, "Extension bit is set!\n");
							fflush(bt_display);
							// clear buffer
							memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
							// send extension handshake
							strcpy((char *)&msgbuffer[2], "d1:md11:ut_metadatai2ee13:metadata_sizei0ee");
							memset(&msgbuffer[0], 0x14, 1); // message id 20
							memset(&msgbuffer[1], 0x0, 1); // handshake id 0
							msgsize = 45;
							sent = SendBTmsg(peer_sock_fd, (char *) &msgbuffer[0], msgsize);
							if (sent == msgsize)
							{
								int got = 0;

								fprintf(bt_display, "Sent %i bytes for extension handshake.\n", sent);
								fflush(bt_display);
								// clear buffer
								memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
								// get extended handshake back
								got = GetBTmsg(peer_sock_fd, (char *) &msgbuffer, 20, 0);
								if (got > 0)
								{
									const char *ptr;

									fprintf(bt_display, "Received %i bytes for extension handshake.\n", got);
									fflush(bt_display);
									ptr = memmem(&msgbuffer[2], got - 2, "11:ut_metadatai", 15);
									if (ptr)
									{
										long msg_id = -1;
										const char *ptr2;

										// get ut_metadata value
										msg_id = atol((char*) ptr + 15);
										fprintf(bt_display, "ut_metadata = %ld\n", msg_id);
										fflush(bt_display);
										// get metadata_size
										ptr2 = memmem(&msgbuffer[2], got - 2, "13:metadata_sizei", 17);
										if (ptr2)
										{
											long metasize = -1;
											int numofpieces = 0;
											int lastpiecesize = 0;
											int counter = 0;
											time_t currenttime;
											struct tm tm;

											// get metadata size
											metasize = atol((char*) ptr2 + 17);
											fprintf(bt_display, "metadata_size = %ld\n", metasize);
											fflush(bt_display);
											numofpieces = metasize / 16384;
											if ((lastpiecesize = metasize % 16384) > 0) numofpieces++;
											fprintf(bt_display, "Number of pieces = %d\n", numofpieces);
											fflush(bt_display);
											fprintf(bt_display, "Last piece size = %d\n", lastpiecesize);
											fflush(bt_display);
											// clear metadatabuffer
											memset(&metadatabuffer[0], 0x0, METADATBUFFERSIZE); 
											// get pieces
											while (counter < numofpieces)
											{
												int sent2 = 0;
												int msgsize2 = 0;

												fprintf(bt_display, "Attempting to get metadata piece %i\n",  counter);
												fflush(bt_display);
												// clear buffer
												memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
												// send get piece message
												sprintf((char *)&msgbuffer[2], "d8:msg_typei0e5:piecei%iee", counter);
												memset(&msgbuffer[0], 0x14, 1); // message id 20
												memset(&msgbuffer[1], msg_id, 1); // ut_metadata id
												if (counter < 10) msgsize2 = 27;
												else msgsize2 = 28;
												sent2 = SendBTmsg(peer_sock_fd, (char *) &msgbuffer[0], msgsize2);
												if (sent2 == msgsize2)
												{
													int got2 = 0;

													fprintf(bt_display, "Sent %i bytes for get metadata piece.\n", sent2);
													fflush(bt_display);
													// clear buffer
													memset(&msgbuffer[0], 0x0, MSGBUFFERSIZE); 
													// get piece back
													got2 = GetBTmsg(peer_sock_fd, (char *) &msgbuffer, 20, 2);
													if (got2 > 0)
													{
														fprintf(bt_display, "Received %i bytes for get metadata piece.\n", got2);
														fflush(bt_display);
														// store piece
														if (counter == numofpieces - 1)
														{
															memcpy(&metadatabuffer[counter * 16384], &msgbuffer[got2-lastpiecesize], lastpiecesize);
														}
														else
														{
															memcpy(&metadatabuffer[counter * 16384], &msgbuffer[got2-16384], 16384);
														}
													}
													else
													{
														if (got2 == 0) 
														{
															fprintf(bt_display, "Disconnected by peer.\n");
															fflush(bt_display);
														}
														else if (got2 == -2) 
														{
															fprintf(bt_display, "Three strikes! You're out. Disconnecting.\n");
															fflush(bt_display);
														}
														else 
														{
															fprintf(bt_display, "Get piece receive failed. Disconnecting.\n");
															fflush(bt_display);
														}
														// exit while loop
														counter = numofpieces;
													}
												}
												else
												{
													if (sent2 == 0) 
													{
														fprintf(bt_display, "Disconnected by peer.\n");
														fflush(bt_display);
													}
													else 
													{
														fprintf(bt_display, "Get piece send failed. Disconnecting.\n");
														fflush(bt_display);
													}
													// exit while loop
													counter = numofpieces;
												}
												counter++;
											}
											if (counter == numofpieces + 1)
											{
												fprintf(bt_display, "Unable to capture all pieces.\n");
												fflush(bt_display);
											}
											else
											{
												unsigned char hash[20];
												int ctr;
												int ptr = 0;

												fprintf(bt_display, "Metadata captured.   Disconnecting peer.\n");
												fflush(bt_display);
												// parse metadata
												if ((!strncmp(&metadatabuffer[0], "d", 1)) && 
												    (!strncmp(&metadatabuffer[metasize - 1], "e", 1)))
												{

													char hashstring[50];
													char namestring[PATH_MAX];
													char lastseen[PATH_MAX];
													long length = 0;
													long private = 0;
													int bHasFiles = FALSE;

													// calculate hash
													SHA1((unsigned char *)&metadatabuffer[0], metasize, hash);
													hashstring[0] = 0;
													for (ctr=0; ctr < 20; ctr++) 
													{
														char temp[50];

														temp[0] = 0;
														sprintf(temp, "%02x", hash[ctr]);
														strcat(hashstring, temp);
													}
													//parse info dictionary
													ptr = 1; // jump over 'd'
													while (ptr < metasize-1)  
													{
														char infoentry[1024];
														long infoentry_len;

														// get a string
														infoentry_len = strtol(&metadatabuffer[ptr], NULL, 10);
														if (infoentry_len < 0) ptr++; // jump minus sign
														while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
														ptr++; // jump ':'
														snprintf(infoentry, infoentry_len + 1, "%s", &metadatabuffer[ptr]);
														ptr = ptr + infoentry_len; // jump to next item
														if (!strcmp(infoentry, "length"))
														{
															ptr++;  //jump over 'i'
															length = strtol(&metadatabuffer[ptr], NULL, 10);
															//g_print(" length = %li\n", length);
															if (length < 0) ptr++; // jump minus sign
															while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
															ptr++;  //jump over 'e'
														}
														else if (!strcmp(infoentry, "pieces"))
														{
															long sizeofpieces;

															// jump over pieces
															sizeofpieces = strtol(&metadatabuffer[ptr], NULL, 10);
															if (sizeofpieces < 0) ptr++; // jump minus sign
															while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
															ptr = ptr + sizeofpieces + 1;
														}
														else if (!strcmp(infoentry, "name"))
														{
															long strlen;

															strlen = strtol(&metadatabuffer[ptr], NULL, 10);
															if (strlen < 0) ptr++; // jump minus sign
															while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
															ptr++; // jump ':'
															snprintf(namestring, strlen + 1, "%s", &metadatabuffer[ptr]);
															//g_print("   name = %s\n", namestring);
															ptr = ptr + strlen;
														}
														else if (!strcmp(infoentry, "private"))
														{
															ptr++;  //jump over 'i'
															private = strtol(&metadatabuffer[ptr], NULL, 10);
															//g_print("private = %li\n", private);
															if (private < 0) ptr++; // jump minus sign
															while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
															ptr++;  //jump over 'e'
														}
														else if (!strcmp(infoentry, "files"))
														{
															int filesloop = TRUE;

															bHasFiles = TRUE;
															//g_print("  Files List:\n");
															ptr++; // jump 'l'
															// list of dictionaries
															while (filesloop)
															{
																if (metadatabuffer[ptr] == 'e') // end of list
																{
																	filesloop = FALSE;
																	ptr++;
																}
																else if (metadatabuffer[ptr] == 'd')
																{
																	int dictloop = TRUE;
																	char pathstring[PATH_MAX];
																	long filelength = 0;

																	pathstring[0] = 0;
																	ptr++;  // jump 'd'
																	// file dictionary
																	while (dictloop)
																	{
																		if (metadatabuffer[ptr] == 'e') // end of dictionary
																		{
																			gchar *sqlcmd2 = "";
																			char *err_msg3 = 0;
																			char filelengthstr[256];
																			char safepathstring[PATH_MAX * 2];
																			int ctr2;
																			int safectr2 = 0;

																			// convert ints to strings
																			sprintf(filelengthstr, "%li", filelength);
																			// double single quotes for pathstring
																			for (ctr2 = 0; ctr2 < strlen(pathstring); ctr2++)
																			{
																				if (pathstring[ctr2] == 39) 
																				{
																					safepathstring[safectr2] = 39;
																					safectr2++;
																				}
																				safepathstring[safectr2] = pathstring[ctr2];
																				safectr2++;
																			}
																			safepathstring[safectr2] = 0;
																			// write to database
																			sqlcmd2 = g_strconcat("REPLACE INTO files (hash,length,name)",
																			                      " VALUES ('",
																			                      hashstring,
																			                      "',",
																			                      filelengthstr,
																			                      ",'",
																			                      safepathstring,
																			                      "');", NULL);
																			if (!((sqlite3_exec(torrent_db, sqlcmd2, NULL, 0, &err_msg3)) == SQLITE_OK))
																			{
																				printf("SQL error: %s\n", err_msg3);
																				sqlite3_free(err_msg3);
																			}
																			usleep(10000);
																			dictloop = FALSE;
																			ptr++;
																		}
																		else 
																		{
																			char dictentry[1024];
																			long dictentry_len;

																			// get a string
																			dictentry_len = strtol(&metadatabuffer[ptr], NULL, 10);
																			if (dictentry_len < 0) ptr++; // jump minus sign
																			while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																			ptr++; // jump ':'
																			snprintf(dictentry, dictentry_len + 1, "%s", &metadatabuffer[ptr]);
																			ptr = ptr + dictentry_len; // jump to next item
																			if (!strcmp(dictentry, "length"))
																			{
																				ptr++;  //jump over 'i'
																				filelength = strtol(&metadatabuffer[ptr], NULL, 10);
																				length = length + filelength;
																				if (filelength < 0) ptr++; // jump minus sign
																				while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																				ptr++;  //jump over 'e'
																			}
																			else if (!strcmp(dictentry, "path"))
																			{
																				// list of strings

																				ptr++; // jump 'l'
																				int pathloop = TRUE;
																				while (pathloop)
																				{
																					if (metadatabuffer[ptr] == 'e') // end of path list
																					{
																						pathloop = FALSE;
																						ptr++;
																					}
																					else 
																					{
																						char tmpstring[PATH_MAX];
																						long tmpstrlen;

																						// get string
																						tmpstrlen = strtol(&metadatabuffer[ptr], NULL, 10);
																						if (tmpstrlen < 0) ptr++; // jump minus sign
																						while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																						ptr++; // jump ':'
																						snprintf(tmpstring, tmpstrlen + 1, "%s", &metadatabuffer[ptr]);
																						strcat(pathstring, tmpstring);
																						ptr = ptr + tmpstrlen;
																						if (!(metadatabuffer[ptr] == 'e')) strcat(pathstring, "/");
																					}
																				}
																				//g_print("   %s\n", pathstring);
																			}
																			else
																			{
																				int level = 512;

																				// jump one item
																				while (level > 0)
																				{
																					int tmpptr;

																					tmpptr = ptr;
																					if (level == 512) level = 0;
																					if (metadatabuffer[tmpptr] == 'd')
																					{
																						level++;
																						ptr++;
																					}
																					else if (metadatabuffer[tmpptr] == 'l')
																					{
																						level++;
																						ptr++;
																					}
																					else if (metadatabuffer[tmpptr] == 'e')
																					{
																						level--;
																						ptr++;
																					}
																					else if (metadatabuffer[tmpptr] == 'i')
																					{
																						long somenum;

																						ptr++;  //jump over 'i'
																						somenum = strtol(&metadatabuffer[ptr], NULL, 10);
																						if (somenum < 0) ptr++; // jump minus sign
																						while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																						ptr++;  //jump over 'e'
																					}
																					else if (isdigit(metadatabuffer[tmpptr]) != 0)
																					{
																						long str_len;

																						str_len = strtol(&metadatabuffer[ptr], NULL, 10);
																						if (str_len < 0) ptr++; // jump minus sign
																						while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																						ptr = ptr + str_len + 1;
																					}
																					else
																					{
																						g_print("Unparsable item!\n");
																						ptr = metasize;
																						dictloop = FALSE;
																						filesloop = FALSE;
																						level = 0;
																					}
																				}
																			}
																		}
																	}
																}
															}
														}
														else
														{
															int level = 512;

															// jump one item
															while (level > 0)
															{
																int tmpptr;

																tmpptr = ptr;
																if (level == 512) level = 0;
																if (metadatabuffer[tmpptr] == 'd')
																{
																	level++;
																	ptr++;
																}
																else if (metadatabuffer[tmpptr] == 'l')
																{
																	level++;
																	ptr++;
																}
																else if (metadatabuffer[tmpptr] == 'e')
																{
																	level--;
																	ptr++;
																}
																else if (metadatabuffer[tmpptr] == 'i')
																{
																	long somenum;

																	ptr++;  //jump over 'i'
																	somenum = strtol(&metadatabuffer[ptr], NULL, 10);
																	if (somenum < 0) ptr++; // jump minus sign
																	while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																	ptr++;  //jump over 'e'
																}
																else if (isdigit(metadatabuffer[tmpptr]) != 0)
																{
																	long str_len;

																	str_len = strtol(&metadatabuffer[ptr], NULL, 10);
																	if (str_len < 0) ptr++; // jump minus sign
																	while (isdigit(metadatabuffer[ptr]) != 0) ptr++;
																	ptr = ptr + str_len + 1;
																}
																else
																{
																	g_print("Unparsable item!\n");
																	//g_print("loc: %s\n", &metadatabuffer[ptr]);
																	ptr = metasize;
																	level = 0;
																}
															}
														}
													}

													{
														gchar *sqlcmd = "";
														char *err_msg2 = 0;
														char lengthstr[256];
														char privatestr[256];
														char safenamestring[PATH_MAX * 2];
														int ctr;
														int safectr = 0;

														// set lastseen
														currenttime = time(NULL);
														tm = *localtime(&currenttime);
														sprintf(lastseen, "%04d%02d%02d%02d%02d%02d", 
														        tm.tm_year + 1900, 
														        tm.tm_mon + 1, 
														        tm.tm_mday, 
														        tm.tm_hour, 
														        tm.tm_min, 
														        tm.tm_sec);
														// convert ints to strings
														sprintf(lengthstr, "%li", length);
														sprintf(privatestr, "%li", private);
														// double single quotes for sql
														for (ctr = 0; ctr < strlen(namestring); ctr++)
														{
															if (namestring[ctr] == 39) 
															{
																safenamestring[safectr] = 39;
																safectr++;
															}
															safenamestring[safectr] = namestring[ctr];
															safectr++;
														}
														safenamestring[safectr] = 0;
														// write to database
														sqlcmd = g_strconcat("REPLACE INTO hash (hash,name,lastseen,length,private)",
														                     " VALUES ('",
														                     hashstring,
														                     "','",
														                     safenamestring,
														                     "','",
														                     lastseen,
														                     "',",
														                     lengthstr,
														                     ",",
														                     privatestr,
														                     ");", NULL);
														if (!((sqlite3_exec(torrent_db, sqlcmd, NULL, 0, &err_msg2)) == SQLITE_OK))
														{
															printf("SQL error: %s\n", err_msg2);
															sqlite3_free(err_msg2);
														}
														usleep(10000);
														if (bHasFiles == FALSE)
														{
															// write name to files table
															sqlcmd = g_strconcat("REPLACE INTO files (hash,length,name)",
															                     " VALUES ('",
															                     hashstring,
															                     "',",
															                     lengthstr,
															                     ",'",
															                     safenamestring,
															                     "');", NULL);
															if (!((sqlite3_exec(torrent_db, sqlcmd, NULL, 0, &err_msg2)) == SQLITE_OK))
															{
																printf("SQL error: %s\n", err_msg2);
																sqlite3_free(err_msg2);
															}
														}
														// if we are replacing current record currentrecord = 0
														//const gchar *currenthash = gtk_label_get_text(HashLabel);
														//if (!g_strcmp0(currenthash, hashstring)) 
														//{
														//	char *err_msg = 0;
														//
															// set current rowid to last
														//	if (!((sqlite3_exec(torrent_db, 
														//	                    "SELECT rowid FROM hash ORDER BY rowid DESC LIMIT 1;",
														//	                    getrowid_ret, 0, &err_msg)) == SQLITE_OK))
														//	{
														//		printf("SQL error: %s\n", err_msg);
														//		sqlite3_free(err_msg);
														//	}
														//	currentrowid = rowid_ret;
														//}
														fprintf(bt_display, "Metadata parsed into database.\n");
														fflush(bt_display);
														usleep(10000);
														display_record();
													}
												}
											}
										}
										else
										{
											fprintf(bt_display, "Unable to get metadata size. Disconnecting.\n");
											fflush(bt_display);
										}
									}
									else
									{
										fprintf(bt_display, "ut_metadata not supported. Disconnecting.\n");										fflush(bt_display);
										fflush(bt_display);
									}
								}
								else
								{
									if (got == 0) 
									{
										fprintf(bt_display, "Disconnected by peer.\n");
										fflush(bt_display);
									}
									else if (got == -2) 
									{
										fprintf(bt_display, "Three strikes! You're out. Disconnecting.\n");
										fflush(bt_display);
									}
									else 
									{
										fprintf(bt_display, "Extension handshake received failed. Disconnecting.\n");
										fflush(bt_display);
									}
								}
							}
							else
							{
								if (sent == 0) 
								{
									fprintf(bt_display, "Disconnected by peer.\n");
									fflush(bt_display);
								}
								else 
								{
									fprintf(bt_display, "Extension handshake send failed. Disconnecting.\n");
									fflush(bt_display);
								}
							}
						}
						else
						{
							fprintf(bt_display, "Peer dosen't support extension bit. Disconnecting.\n");
							fflush(bt_display);
						}
					}
					else
					{
						if (hshake_recd == 0)
						{
							fprintf(bt_display, "Disconnected by peer.\n");
							fflush(bt_display);
						}
						else
						{
							fprintf(bt_display, "Handshake not received. Disconnecting.\n");
							fflush(bt_display);
						}
					}
				}
				else
				{
					if (hshake_sent == 0)
					{
						fprintf(bt_display, "Disconnected by peer.\n");
						fflush(bt_display);
					}
					else
					{
						fprintf(bt_display, "Handshake send failed. Disconnecting.\n");
						fflush(bt_display);
					}
				}
			}
			else 
			{
				fprintf(bt_display, "Peer connect from slot %i failed.\n", peerlistnum);
				fflush(bt_display);
			}
			close(peer_sock_fd);
			// clear slot
			memset(&PeerListQueue[peerlistnum], 0, sizeof(struct peerlist));
		}
		sleep(1);
		// keep looking through peers
		peerlistnum++;
		if (peerlistnum == MAX_PEERPROSPECTS) peerlistnum = 0;
	}
	return 0; 
}

/****************************************************************************
 *                                                                          *
 * Function: CaptureAnnounce                                                *
 *                                                                          *
 * Purpose : Called whenever DHT sees an announce peer. Puts info hash into *
 *           a PeerListQueue slot                                           *
 *                                                                          *
 ****************************************************************************/
void CaptureAnnounce(unsigned char *hash, const struct sockaddr *fromaddr, unsigned short prt)
{
	if (fromaddr->sa_family == AF_INET) 
	{
		char temp[100];
		unsigned short tmpport;
		struct sockaddr_in *temp_sin = (struct sockaddr_in*)fromaddr;

		tmpport = htons(prt);
		// add to address to peerlist queuue
		memcpy(&PeerListQueue[peerlistopenslot].info_hash[0], &hash[0], 20);
		memcpy(&PeerListQueue[peerlistopenslot].addr, &(temp_sin->sin_addr), 4);
		memcpy(&PeerListQueue[peerlistopenslot].prt, &tmpport, 2);
		inet_ntop(AF_INET, &PeerListQueue[peerlistopenslot].addr, 
		          &temp[0], sizeof(temp));
		fprintf(bt_display, "Peer captured slot %i:  Address: %s  Port: %d\n", 
		        peerlistopenslot, temp,
		        ntohs(PeerListQueue[peerlistopenslot].prt));
		fflush(bt_display);
		peerlistopenslot++;
		if (peerlistopenslot == MAX_PEERPROSPECTS) peerlistopenslot = 0;
	}
	else
	{
		fprintf(bt_display, "Ipv6 Address - peer not captured.\n");
		fflush(bt_display);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: ReadMessages                                                       *
 *                                                                            *
 * Purpose :                                                                  *
 *                                                                            *
 ******************************************************************************/
gboolean ReadMessages (void)
{
	char c;
	int ctr = 0;
	char readbuffer[256];

	// read dht messages
	if (read (dhtpipe[0], &c, 1) == 1) // we have something to read
	{
		while ((c != '\n') && (c != '\0') && (ctr < 255))
		{
			readbuffer[ctr] = c;
			read (dhtpipe[0] ,&c, 1);
			ctr++;
		}
		readbuffer[ctr] = '\0';
		if (ctr > 0) 
		{
			GtkTextIter iter;
			GtkAdjustment *vadj;
			gchar *temp;

			// write to the textbuffer
			temp = g_strconcat(readbuffer, "\n", NULL);
			gtk_text_buffer_get_end_iter (DHTtextbuffer, &iter);
			gtk_text_buffer_insert (DHTtextbuffer, &iter, temp, -1);

			// scroll to bottom
			vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
			                                           (DHTWindowScrollView));
			gtk_adjustment_set_value (GTK_ADJUSTMENT(vadj), gtk_adjustment_get_upper 
			                          (GTK_ADJUSTMENT(vadj)));
			gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW
			                                    (DHTWindowScrollView), 
			                                    GTK_ADJUSTMENT (vadj));
		}
	}
	// read bt messages
	ctr = 0;
	if (read (btpipe[0], &c, 1) == 1) // we have something to read
	{
		while ((c != '\n') && (c != '\0') && (ctr < 255))
		{
			readbuffer[ctr] = c;
			read (btpipe[0] ,&c, 1);
			ctr++;
		}
		readbuffer[ctr] = '\0';
		if (ctr > 0) 
		{
			GtkTextIter iter;
			GtkAdjustment *vadj;
			gchar *temp;

			// write to the textbuffer
			temp = g_strconcat(readbuffer, "\n", NULL);
			gtk_text_buffer_get_end_iter (BTtextbuffer, &iter);
			gtk_text_buffer_insert (BTtextbuffer, &iter, temp, -1);

			// scroll to bottom
			vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
			                                           (BTWindowScrollView));
			gtk_adjustment_set_value (GTK_ADJUSTMENT(vadj), gtk_adjustment_get_upper 
			                          (GTK_ADJUSTMENT(vadj)));
			gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW
			                                    (BTWindowScrollView), 
			                                    GTK_ADJUSTMENT (vadj));
		}
	}
	return TRUE; // keep running
}

/****************************************************************************
 *                                                                          *
 * Function: callback                                                       *
 *                                                                          *
 * Purpose : The call-back function is called by the DHT whenever something *
 *           interesting happens.  Right now, it only happens when we get a *
 *           new value or when a search completes, but this may be extended *
 *           in future versions.                                            *
 *                                                                          *
 ****************************************************************************/
static void callback(void *closure,
                     int event,
                     unsigned char *info_hash,
                     void *data, size_t data_len)
{

}

/****************************************************************************
 *                                                                          *
 * Function: dht_blacklisted                                                *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int dht_blacklisted(const struct sockaddr *sa, int salen)
{
	return 0;
}

/****************************************************************************
 *                                                                          *
 * Function: dht_hash                                                       *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
void dht_hash(void *hash_return, int hash_size,
              const void *v1, int len1,
              const void *v2, int len2,
              const void *v3, int len3)
{
	const char *c1 = v1, *c2 = v2, *c3 = v3;
	char key[9];                /* crypt is limited to 8 characters */
	int i;

	memset(key, 0, 9);

	for(i = 0; i < 2 && i < len1; i++)
		key[i] = CRYPT_HAPPY(c1[i]);
	for(i = 0; i < 4 && i < len1; i++)
		key[2 + i] = CRYPT_HAPPY(c2[i]);
	for(i = 0; i < 2 && i < len1; i++)
		key[6 + i] = CRYPT_HAPPY(c3[i]);
	strncpy(hash_return, crypt(key, "dd"), hash_size);
}

/****************************************************************************
 *                                                                          *
 * Function: dht_random_bytes                                               *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int dht_random_bytes(void *buf, size_t size)
{
	int fd, rc, save;

	fd = open("/dev/urandom", O_RDONLY);
	if(fd < 0) return -1;
	rc = read(fd, buf, size);
	save = errno;
	close(fd);
	errno = save;
	return rc;
}

/****************************************************************************
 *                                                                          *
 * Function: DhtThread                                                      *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
gint DhtThread(void)
{
	unsigned char myid[20];
	unsigned char seedid[20];
	int port;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr_in Seed;
	struct sockaddr_in6 Seed6;
	time_t tosleep = 0;
	int rc;
	struct sockaddr_storage from;
	socklen_t fromlen;

	// create random ids
	dht_random_bytes(myid, 20);
	dht_random_bytes(seedid, 20);

	// set random port between 2000 - 65535
	port = rand() % 63536 + 2000;
	fprintf(dht_debug, "DHT port set to: %d.\n", port);
	fflush(dht_debug);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if(s < 0) 
	{
		fprintf(dht_debug, "IPv4 socket failed.\n");
		fflush(dht_debug);
	}
	if(s >= 0) 
	{
		if (!bind(s, (struct sockaddr*)&sin, sizeof(sin)))
		{
			fprintf(dht_debug, "IPv4 socket bound to dht port.\n");
			fflush(dht_debug);
		}
	}
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	s6 = socket(PF_INET6, SOCK_DGRAM, 0);
	if(s6 < 0) {
		fprintf(dht_debug, "IPv6 socket failed.\n");
		fflush(dht_debug);
	}
	if(s6 >= 0) {
		int val = 1;

		setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY,
		           (char *)&val, sizeof(val));
		if (!bind(s6, (struct sockaddr*)&sin6, sizeof(sin6)))
		{
			fprintf(dht_debug, "IPv6 socket bound to dht port.\n");
			fflush(dht_debug);
		}
	}
	if ((s < 0) && (s6 < 0))
	{
		fprintf(dht_debug, "No DHT sockets setup! Exiting....\n");
		fflush(dht_debug);
		return 0;
	}

	// init dht
	dht_init(s, s6, myid, (unsigned char*)"digg");
	// setup bootstrap
	if(s >= 0) 
	{
		fprintf(dht_debug, "Seeding IPv4 with dht.transmissionbt.com node.\n");
		fflush(dht_debug);
		memset(&Seed, 0, sizeof(Seed));
		Seed.sin_family = AF_INET;         
		Seed.sin_port = htons(6881); 
		inet_pton(AF_INET, "87.98.162.88", &(Seed.sin_addr.s_addr));
		dht_insert_node(seedid, (struct sockaddr*) &Seed, sizeof(Seed));
		if (Bootstrap.numofIPv4s > 0)
		{
			int ctr = 0;

			fprintf(dht_debug, "Pinging %i saved nodes.\n", Bootstrap.numofIPv4s);
			fflush(dht_debug);
			for (ctr = 0; ctr < Bootstrap.numofIPv4s;  ctr++)
			{
				dht_ping_node((struct sockaddr*) &Bootstrap.IPv4bootnodes[ctr], 
				              sizeof(struct sockaddr_in));
			}
		}
	}
	if(s6 >= 0)
	{
		fprintf(dht_debug, "Seeding IPv6 with dht.transmissionbt.com node.\n");
		fflush(dht_debug);
		memset(&Seed6, 0, sizeof(Seed6));
		Seed6.sin6_family = AF_INET6;         
		Seed6.sin6_port = htons(6881); 
		inet_pton(AF_INET6, "2001:41d0:c:5ac:5::1", &(Seed6.sin6_addr));
		dht_insert_node(seedid, (struct sockaddr*) &Seed6, sizeof(Seed6));
		if (Bootstrap.numofIPv6s > 0)
		{
			int ctr = 0;

			fprintf(dht_debug, "Pinging %i saved IPv6 nodes.\n", Bootstrap.numofIPv6s);
			fflush(dht_debug);
			for (ctr = 0; ctr < Bootstrap.numofIPv6s;  ctr++)
			{
				dht_ping_node((struct sockaddr*) &Bootstrap.IPv6bootnodes[ctr], 
				              sizeof(struct sockaddr_in6));
			}
		}
	}
	// start dht loop
	while(dhtloop) 
	{
		struct timeval tv;
		fd_set readfds;
		tv.tv_sec = tosleep;
		tv.tv_usec = random() % 1000000;

		FD_ZERO(&readfds);
		if(s >= 0)
			FD_SET(s, &readfds);
		if(s6 >= 0)
			FD_SET(s6, &readfds);
		rc = select(s > s6 ? s + 1 : s6 + 1, &readfds, NULL, NULL, &tv);
		if(rc < 0) {
			if(errno != EINTR) {
				perror("select");
				sleep(1);
			}
		}
		if(rc > 0) {
			fromlen = sizeof(from);
			if(s >= 0 && FD_ISSET(s, &readfds))
				rc = recvfrom(s, buf, sizeof(buf) - 1, 0,
				              (struct sockaddr*)&from, &fromlen);
			else if(s6 >= 0 && FD_ISSET(s6, &readfds))
				rc = recvfrom(s6, buf, sizeof(buf) - 1, 0,
				              (struct sockaddr*)&from, &fromlen);
			else
				abort();
		}
		if(rc > 0) {
			buf[rc] = '\0';
			rc = dht_periodic(buf, rc, (struct sockaddr*)&from, fromlen,
			                  &tosleep, (dht_callback *) callback, NULL);
		} else {
			rc = dht_periodic(NULL, 0, NULL, 0, &tosleep, (dht_callback *) callback, NULL);
		}
		if(rc < 0) {
			if(errno == EINTR) {
				continue;
			} else {
				perror("dht_periodic");
				if(rc == EINVAL || rc == EFAULT)
					abort();
				tosleep = 1;
			}
		}
	}
	dht_uninit ();
	close(s);
	close(s6);
	return 0; 
}

/****************************************************************************
 *                                                                          *
 * Function: MainWindowDestroy                                              *
 *                                                                          *
 * Purpose : Called when the window is closed                               *
 *                                                                          *
 ****************************************************************************/
void MainWindowDestroy (GtkWidget *widget, gpointer data)
{
	// close sqlite3
	sqlite3_close(torrent_db);
	// kill loops 
	dhtloop = FALSE;
	getmetadataloop = FALSE;
	// kill gtk loop
	gtk_main_quit ();
}
/****************************************************************************
 *                                                                          *
 * Function: CreateMainWindow                                               *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
static GtkWidget* CreateMainWindow (void)
{
	GtkWidget *window;
	GtkBuilder *builder;
	GError* error = NULL;

	// Load UI from file
	builder = gtk_builder_new ();
	if (gtk_builder_add_from_file (builder, UI_FILE, &error))
	{
		// Auto-connect signal handlers
		gtk_builder_connect_signals (builder, NULL);
		// Get the window objects from the ui file
		window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
		DHTtextbuffer = GTK_TEXT_BUFFER (gtk_builder_get_object 
		                                 (builder, "textbuffer1"));
		BTtextbuffer = GTK_TEXT_BUFFER (gtk_builder_get_object 
		                                (builder, "textbuffer2"));
		DHTWindowScrollView = GTK_SCROLLED_WINDOW (gtk_builder_get_object 
		                                           (builder, "scrolledwindow1"));
		BTWindowScrollView = GTK_SCROLLED_WINDOW (gtk_builder_get_object 
		                                          (builder, "scrolledwindow2"));
		TorrentNameLabel = GTK_LABEL (gtk_builder_get_object (builder, "torrent_name"));

		HashLabel = GTK_LABEL (gtk_builder_get_object (builder, "hash_label"));
		LastSeenLabel = GTK_LABEL (gtk_builder_get_object (builder, "last_seen"));
		LengthLabel = GTK_LABEL (gtk_builder_get_object (builder, "length"));
		PrivateLabel = GTK_LABEL (gtk_builder_get_object (builder, "private_label"));
		FileList = GTK_LIST_STORE (gtk_builder_get_object (builder, "liststore1"));
		RecordCount = GTK_LABEL (gtk_builder_get_object (builder, "record_count"));
		MagnetLink = GTK_LINK_BUTTON (gtk_builder_get_object (builder, "magnetlink"));
		// unload builder
		g_object_unref (builder);
	}
	else
	{
		g_print ("Unable to load dhtdigg.ui. Reinstall dhtdigg.\n"); 
		exit(1);
	}
	return window;
}

/****************************************************************************
 *                                                                          *
 * Function: main                                                           *
 *                                                                          *
 * Purpose :                                                                *
 *                                                                          *
 ****************************************************************************/
int main (int argc, char *argv[])
{
	GtkWidget *window;
	int flags;
	gchar *bsfilename;
	gchar *dbfilename;

	// init gtk
	gtk_init (&argc, &argv);
	// create main window
	window = CreateMainWindow ();
	// show main window
	gtk_widget_show (window);
	// seed rand
	srand((unsigned) time(NULL));
	// create pipe from dht to gui
	pipe(dhtpipe);
	// set read side to non-blocking
	flags = fcntl(dhtpipe[0], F_GETFL, 0);
	fcntl(dhtpipe[0], F_SETFL, flags | O_NONBLOCK);
	// redirect dht_debug to the pipe
	dht_debug = fdopen(dhtpipe[1], "w");
	// create pipe for BT messages
	pipe(btpipe);
	// set read side to non-blocking
	flags = fcntl(btpipe[0], F_GETFL, 0);
	fcntl(btpipe[0], F_SETFL, flags | O_NONBLOCK);
	// open pipe
	bt_display = fdopen(btpipe[1], "w");
	// clear queue
	memset(&PeerListQueue, 0, sizeof(PeerListQueue));
	// set work directory
	workdir = g_strconcat (g_get_home_dir (), "/.dhtdigg/", NULL);
	// make sure work directory exists
	if (stat(workdir, &st) == -1) mkdir(workdir, 0700);
	// clear bootstrap storage
	memset(&Bootstrap, 0, sizeof(Bootstrap));
	//set bootstrap  file name
	bsfilename = g_strconcat (workdir, "dhtdigg.bs", NULL);
	if (stat(bsfilename, &st) == 0) 
	{
		int bsfile_fd;

		// open bootstrap file
		bsfile_fd = open(bsfilename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
		read(bsfile_fd, &Bootstrap, sizeof(struct bootstrap_storage));
		close(bsfile_fd);
	}
	//set database file name
	dbfilename = g_strconcat (workdir, "dhtdigg.db", NULL);
	//open database
	if (stat(dbfilename, &st) == 0)
	{
		//exists
		if (sqlite3_open(dbfilename, &torrent_db))
		{
			printf("Database open error: %s\n", sqlite3_errmsg(torrent_db));
		}
		else
		{
			fprintf(bt_display, "Opening existing database %s\n", dbfilename);
			fflush(bt_display);
		}
	}
	else
	{
		if (sqlite3_open(dbfilename, &torrent_db))
		{
			printf("Database open error: %s\n", sqlite3_errmsg(torrent_db));
		}
		else 
		{
			char *err_msg = 0;

			fprintf(bt_display, "Creating new database %s\n", dbfilename);
			fflush(bt_display);
			// create tables
			if (!((sqlite3_exec(torrent_db,
			                    "CREATE TABLE dhtdigg ("  \
			                    "dbversion TEXT);" \
			                    "CREATE TABLE hash ("  \
			                    "hash TEXT PRIMARY KEY UNIQUE NOT NULL, " \
			                    "name TEXT, " \
			                    "lastseen TIME, " \
			                    "length INTEGER, " \
			                    "private INTEGER);" \
			                    "CREATE TABLE files ("  \
			                    "hash TEXT, " \
			                    "length INTEGER, " \
			                    "name TEXT, " \
			                    "UNIQUE (hash, length, name)" \
			                    ");"
			                    , NULL, 0, &err_msg)) == SQLITE_OK))
			{
				printf("SQL error: %s\n", err_msg);
				sqlite3_free(err_msg);
			}
			// set database version
			if (!((sqlite3_exec(torrent_db,
			                    "REPLACE INTO dhtdigg (dbversion) VALUES ('1.0')",
			                    NULL, 0, &err_msg)) == SQLITE_OK))
			{
				printf("SQL error: %s\n", err_msg);
				sqlite3_free(err_msg);
			}
		}
	}
	fprintf(bt_display, "Waiting for DHT to populate. This could take 10 minutes or so....\n");
	fflush(bt_display);
	// start dht thread
	g_thread_new ("dhtthread", (GThreadFunc) DhtThread, NULL);
	// start getmetadata thread
	g_thread_new ("getmetadatathread", (GThreadFunc) GetMetadataThread, NULL);
	// start read messages thread
	gdk_threads_add_timeout (50, (GSourceFunc) ReadMessages, NULL);
	// dht restart timer 32 minutes
	gdk_threads_add_timeout_seconds (1920, (GSourceFunc) RestartDHT, NULL);
	// autosave bootstrap after 2 minutes
	gdk_threads_add_timeout_seconds (120, (GSourceFunc) WriteBootstrapFile, NULL);
	// start main loop
	//testfunction();
	display_record();
	gtk_main ();
	return 0;
}
