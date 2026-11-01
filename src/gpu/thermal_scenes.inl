namespace strefa::gpu {
void ParticleWorld::reset_thermal_scene(unsigned scene){
    if(scene<1||scene>4)throw std::invalid_argument{"Thermal scene: 1..4"};
    auto options=thermal_options();options.enabled=true;set_thermal_options(options);
    std::vector<std::uint32_t> walls(width*height);std::vector<Grain> grains;std::vector<std::pair<sim::Material,float>> tags;
    const auto box=[&](int x0,int y0,int x1,int y1,unsigned material){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)walls[y*width+x]=material;};
    box(0,187,319,191,1);box(40,45,41,186,1);box(278,45,279,186,1);box(40,45,279,46,1);box(120,105,199,107,2);
    const auto fill=[&](int x0,int y0,int nx,int ny,unsigned kind,sim::Material material,float temperature){for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){const auto [px,py]=seeded_position(x0+x,y0+y);Grain p;p.position={px,py,px,py};p.contact={0,0,px,py};p.motion={0,0,kind==5?2.65F:kind==6?3.3125F:.35F,static_cast<float>(kind)};grains.push_back(p);tags.emplace_back(material,temperature);}};
    if(scene==2||scene==3)fill(100,171,100,10,6,sim::Material::oil,700.F);
    if(scene==3)fill(105,82,90,12,5,sim::Material::water,280);
    if(scene==4){fill(70,140,15,8,3,sim::Material::ice,260);fill(110,140,15,8,6,sim::Material::lava,1650);fill(150,140,15,8,3,sim::Material::molten_metal,2000);fill(190,140,30,8,5,sim::Material::water,360);}
    load(grains,walls);auto& s=*impl_;auto snap=s.thermal->snapshot();
    for(unsigned i=0;i<tags.size();++i){const auto [m,t]=tags[i];auto& p=snap.particles[i];p.matter[1]=p.matter[0]*static_cast<float>(sim::specific_enthalpy(m,t));p.matter[2]=sim::properties(m).burn_rate>0?p.matter[0]:0;p.matter[3]=static_cast<float>(m);p.history[2]=t;}
    for(int y=105;y<=107;++y)for(int x=120;x<=199;++x){auto& c=snap.cells[y*width+x];c.matter[1]=c.matter[0]*static_cast<float>(sim::specific_enthalpy(sim::Material::wood,scene==4?293.15:800));}
    s.thermal->seed(snap.cells,snap.particles);s.thermal_brush={};render(1);
}
}
