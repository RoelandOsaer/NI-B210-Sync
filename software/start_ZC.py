from paramiko import SSHClient
import paramiko
from scp import SCPClient
from tqdm import tqdm
import yaml
import threading
import os

pbar = None

with open("settings.yml", 'r') as stream, open("secrets.yml", 'r') as secret:
    settings = yaml.safe_load(stream)
    secrets = yaml.safe_load(secret)

    recievers_IP = settings["RX"]
    transmitter_IP = settings["TX"]  #TODO currently assumes one TX only
    server_IP = settings["server"]

    rx_root = settings["RX_root"]
    tx_root = settings["TX_root"]
    server_root = settings["server_root"]

    rates = settings["rates"]

    # create SSH connections
    recievers_SSH = [SSHClient() for rx_ssh in recievers_IP]

    for rx_ssh, rx_ip in zip(recievers_SSH,recievers_IP):
        rx_ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        rx_ssh.connect(rx_ip,username="pi",password=secrets["password"],port=22)

    tx_ssh = SSHClient()
    tx_ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    tx_ssh.connect(transmitter_IP, username="pi", password=secrets["password"], port=22)

    server_ssh = SSHClient()
    server_ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    server_ssh.connect(server_IP, username="pi", password=secrets["password"], port=22)

    # # start server
    # def start_server():
    #     stdin, stdout, stderr = rx_ssh.exec_command(f"cd {server_root} && ./server-sync.py")

    # threading.Thread(target=start_server).start()

    if type(rates) is not list:
        rates = [rates]
        

    # Define progress callback that prints the current percentage completed for the file
    def progress(filename, size, sent):
        global pbar
        if pbar is None:
            pbar = tqdm(total=size, desc=f"Downloading to {filename}", unit="B", unit_scale=True, unit_divisor=1024, miniters=1, dynamic_ncols=True)
            pbar.refresh()
            pbar.update(round(float(sent),2)-pbar.n)
            pbar.refresh()


    for rate in rates:
        # start TX and RX
        cmd = f"git pull && mkdir -p build && cd build && cmake ../ && make && ./init_usrp --server-ip='{server_IP}' --rate={rate}"
        def start(ssh, root,ip):
            print(f"cd '{root}' && {cmd}")
            stdin, stdout, stderr = ssh.exec_command(f"cd '{root}' && {cmd}")
            exit_status = stdout.channel.recv_exit_status()
            if exit_status == 0:
                print ("Done")
            else:
                print("Error", exit_status)
                print(stderr.read())
            print(f"[{ip}] {stdout.read()}")

        t_tx = threading.Thread(target=start, args=(tx_ssh, tx_root, transmitter_IP))
        t_tx.start()

        threads_rx = []
        for rx_ssh,ip in zip(recievers_SSH,recievers_IP):
            t_rx = threading.Thread(target=start, args=(rx_ssh, rx_root,ip))
            threads_rx.append(t_rx)
            t_rx.start()

         # wait till done
        for t in threads_rx:
            t.join()

        t_tx.join()
       

        # collect the data
        
        for rx_ssh in recievers_SSH:
            stdin, stdout, stderr = rx_ssh.exec_command(f"find {rx_root} -type f -name '*.dat' -print")
            response = stdout.read()
            files = response.splitlines()
            #print(files)

            # SCPCLient takes a paramiko transport as an argument
            with SCPClient(rx_ssh.get_transport(), progress=progress) as scp:
                for f in files:
                    pbar = None
                    local_path = f'./results/{rate}Sps'
                    if not os.path.exists(local_path):
                        # if the demo_folder directory is not present 
                        # then create it.
                        os.makedirs(local_path)
                    scp.get(f,local_path=local_path)
