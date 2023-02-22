make
read -p "Compilazione eseguita. Premi invio per eseguire..." input

# 2. ESECUZIONE
# I file eseguibili devono chiamarsi come descritto in specifica, e cioè:
#    a) 'server' per il server;
#    b) 'td' per il table device;
#    c) 'kd' per il kitchen device;
#    d) 'cli' per il client.
# I file eseguibili devono essere nella current folder

# 2.1 esecuzione del server sulla porta 4242
  gnome-terminal -- sh -c "./server/server 4242; exec bash"

# # 2.2 esecuzione di 3 table device
  gnome-terminal -- sh -c "./td 4242; exec bash"
  gnome-terminal -- sh -c "./td 4242; exec bash"
  gnome-terminal -- sh -c "./td 4242; exec bash"

# # 2.3 esecuzione di 2 kitchen device
 	gnome-terminal -- sh -c "./kd 4242; exec bash"
	gnome-terminal -- sh -c "./kd 4242; exec bash"

# 2.4 esecuzione di un client
	gnome-terminal -- sh -c "./cli 4242; exec bash"
