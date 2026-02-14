python3 palette_generator.py
cp palette.h ../src/

python3 convert_sprites_to_bin.py
cp sprites.h ../src/

python3 convert_textures_to_bin.py
cp textures.h ../src/

python3 convert_image.py

python3 visualize_palette.py