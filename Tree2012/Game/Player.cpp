#include "pch.h"
#include "Player.h"


Player::Player(WorldObjectParams* params) : WorldObject(params)					   
{
	m_camera = new Camera(this);
}


Player::~Player(void)
{
}
