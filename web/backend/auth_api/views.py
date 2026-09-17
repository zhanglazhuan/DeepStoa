from rest_framework.decorators import api_view
from rest_framework.response import Response

@api_view(['GET'])
def user_info(request):
    user = request.user
    if not user.is_authenticated:
        return Response({'is_authenticated': False}, status=401)
    return Response({
        'id': user.id,
        'email': user.email,
        'first_name': user.first_name,
        'last_name': user.last_name,
        'is_authenticated': True,
    })
