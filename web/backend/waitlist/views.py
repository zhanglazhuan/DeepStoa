from rest_framework import generics, status
from rest_framework.response import Response
from .models import WaitlistEntry
from .serializers import WaitlistEntrySerializer

class WaitlistCreateView(generics.CreateAPIView):
    queryset = WaitlistEntry.objects.all()
    serializer_class = WaitlistEntrySerializer

    def create(self, request, *args, **kwargs):
        serializer = self.get_serializer(data=request.data)
        if serializer.is_valid():
            serializer.save()
            return Response(serializer.data, status=status.HTTP_201_CREATED)
        if 'email' in serializer.errors:
            return Response(
                {'error': 'This email is already registered on the waitlist.'},
                status=status.HTTP_400_BAD_REQUEST,
            )
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)
