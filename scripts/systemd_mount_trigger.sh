[Unit]
Description=Backup <src> to USB drive.
Requires=<mount>
After=<mount>

[Service]
Type=oneshot
ExecStart=beak push -q <src> <path>

[Install]
WantedBy=<mount>
